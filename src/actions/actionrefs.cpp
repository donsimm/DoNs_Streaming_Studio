#include "actionrefs.h"

#include "steptypes.h"
#include "triggertypes.h"

#include <QHash>
#include <QJsonArray>

namespace dss::actions {

namespace {

QString pathText(const QList<int> &path)
{
    QStringList parts;
    for (int n : path)
        parts.append(QString::number(n));
    return parts.join(QLatin1Char('.'));
}

QString stepLabel(const Step &step)
{
    const StepTypeInfo *info = stepType(step.type);
    return info ? info->label : step.type;
}

QString triggerLabel(const Trigger &trigger)
{
    const TriggerTypeInfo *info = triggerType(trigger.type);
    return info ? info->label : trigger.type;
}

QUuid idField(const QJsonObject &fields, const QString &key)
{
    return QUuid::fromString(fields.value(key).toString());
}

// Platzhalter im Wert: Der Verweis steht erst im Lauf fest
bool hasPlaceholder(const QString &text)
{
    return text.contains(QLatin1Char('%')) || text.contains(QLatin1Char('~'));
}

class Checker
{
public:
    Checker(const ActionsConfig &config, const ReferenceContext &context) : m_config(config), m_context(context) {}

    QList<ReferenceWarning> run()
    {
        for (const Action &a : m_config.actions)
            checkAction(a);
        checkCommands();
        return m_warnings;
    }

private:
    void warn(const QUuid &object, const QUuid &item, const QString &message)
    {
        m_warnings.append({object, item, message});
    }

    void checkAction(const Action &a)
    {
        const QString where = QStringLiteral("Aktion «%1»").arg(a.name);
        if (!a.queue.isNull() && !m_config.queue(a.queue))
            warn(a.id, QUuid(), QStringLiteral("%1: Warteschlange fehlt").arg(where));
        for (qsizetype i = 0; i < a.triggers.size(); ++i) {
            const Trigger &t = a.triggers.at(i);
            const QString at = QStringLiteral("%1, Auslöser %2 (%3)").arg(where).arg(i + 1).arg(triggerLabel(t));
            if (t.type == QLatin1String(kTriggerCommand))
                checkTarget(a.id, t.id, at, idField(t.fields, QStringLiteral("command_id")),
                            m_config.command(idField(t.fields, QStringLiteral("command_id"))) != nullptr,
                            t.enabled, QStringLiteral("kein Befehl gewählt"), QStringLiteral("Befehl fehlt"));
            else if (t.type == QLatin1String(kTriggerTimer))
                checkTarget(a.id, t.id, at, idField(t.fields, QStringLiteral("timer_id")),
                            m_config.timer(idField(t.fields, QStringLiteral("timer_id"))) != nullptr,
                            t.enabled, QStringLiteral("keine Zeitnachricht gewählt"),
                            QStringLiteral("Zeitnachricht fehlt"));
        }
        forEachStep(a.steps, [&](const Step &s, const QList<int> &path) {
            checkStep(a, s, QStringLiteral("%1, Schritt %2 (%3)").arg(where, pathText(path), stepLabel(s)));
        });
    }

    // Verweis über eine Kennung: nicht gewählt (nur gemeldet, wenn eingeschaltet) oder fehlt
    void checkTarget(const QUuid &object, const QUuid &item, const QString &at, const QUuid &target,
                     bool found, bool enabled, const QString &notChosen, const QString &missing)
    {
        if (target.isNull()) {
            if (enabled)
                warn(object, item, QStringLiteral("%1: %2").arg(at, notChosen));
        } else if (!found) {
            warn(object, item, QStringLiteral("%1: %2").arg(at, missing));
        }
    }

    void checkMedia(const QUuid &object, const QUuid &item, const QString &at, const QString &file,
                    const QString &what)
    {
        if (!m_context.mediaFiles || file.isEmpty() || hasPlaceholder(file)
            || !file.startsWith(QStringLiteral("media/")))
            return;
        if (!m_context.mediaFiles->contains(file))
            warn(object, item, QStringLiteral("%1: %2 fehlt in den Medien (%3)").arg(at, what, file));
    }

    void checkStep(const Action &a, const Step &s, const QString &at)
    {
        const QJsonObject &f = s.fields;
        if (s.type == QLatin1String(kStepRunAction)) {
            const QUuid target = idField(f, QStringLiteral("action_id"));
            checkTarget(a.id, s.id, at, target, m_config.action(target) != nullptr, s.enabled,
                        QStringLiteral("keine Aktion gewählt"), QStringLiteral("Ziel-Aktion fehlt"));
        } else if (s.type == QLatin1String(kStepIf)) {
            if (f.value(QStringLiteral("operation")).toString() == QLatin1String("regex"))
                warn(a.id, s.id, QStringLiteral("%1: Vergleich «regex» wird gespeichert, aber nicht ausgeführt").arg(at));
        } else if (s.type == QLatin1String(kStepPlaySound)) {
            const QString file = f.value(QStringLiteral("file")).toString();
            if (file.isEmpty() && s.enabled)
                warn(a.id, s.id, QStringLiteral("%1: keine Datei gewählt").arg(at));
            checkMedia(a.id, s.id, at, file, QStringLiteral("Datei"));
        } else if (s.type == QLatin1String(kStepOverlaySource)) {
            checkOverlay(a, s, at);
        }
    }

    void checkOverlay(const Action &a, const Step &s, const QString &at)
    {
        const QUuid overlay = idField(s.fields, QStringLiteral("overlay"));
        const QJsonArray elements = s.fields.value(QStringLiteral("elements")).toArray();
        if (overlay.isNull()) {
            if (s.enabled)
                warn(a.id, s.id, QStringLiteral("%1: kein Overlay gewählt").arg(at));
            return;
        }
        if (elements.isEmpty() && s.enabled)
            warn(a.id, s.id, QStringLiteral("%1: kein Element gewählt").arg(at));
        if (m_context.overlays) {
            const auto it = m_context.overlays->constFind(overlay);
            if (it == m_context.overlays->constEnd()) {
                warn(a.id, s.id, QStringLiteral("%1: Overlay fehlt").arg(at));
            } else {
                for (qsizetype i = 0; i < elements.size(); ++i) {
                    const QUuid source = idField(elements.at(i).toObject(), QStringLiteral("source"));
                    if (!it->contains(source))
                        warn(a.id, s.id, QStringLiteral("%1: Element %2 fehlt im Overlay").arg(at).arg(i + 1));
                }
            }
        }
        for (qsizetype i = 0; i < elements.size(); ++i)
            checkMedia(a.id, s.id, at, elements.at(i).toObject().value(QStringLiteral("image")).toString(),
                       QStringLiteral("Bild von Element %1").arg(i + 1));
    }

    void checkCommands()
    {
        // Befehl in Kleinbuchstaben → erster aktiver Befehl, der ihn trägt
        QHash<QString, const Command *> seen;
        for (const Command &c : m_config.commands) {
            const QString where = QStringLiteral("Befehl «%1»").arg(c.name);
            if (c.mode == QLatin1String("regex"))
                warn(c.id, QUuid(), QStringLiteral("%1: Modus «regex» wird gespeichert, aber nicht ausgeführt").arg(where));
            for (const QUuid &g : c.permissions.groups) {
                if (!m_config.group(g))
                    warn(c.id, QUuid(), QStringLiteral("%1: Benutzergruppe fehlt").arg(where));
            }
            if (!c.enabled)
                continue;
            for (const QString &text : c.commands) {
                const QString key = text.toLower();
                if (const Command *other = seen.value(key); other && other != &c)
                    warn(c.id, QUuid(), QStringLiteral("%1: «%2» auch in «%3»").arg(where, text, other->name));
                else
                    seen.insert(key, &c);
            }
        }
    }

    const ActionsConfig &m_config;
    const ReferenceContext &m_context;
    QList<ReferenceWarning> m_warnings;
};

} // namespace

QList<ReferenceWarning> checkReferences(const ActionsConfig &config, const ReferenceContext &context)
{
    return Checker(config, context).run();
}

} // namespace dss::actions
