#include "auswertung.h"

#include <algorithm>
#include <cmath>
#include <map>

Kennzahlen kennzahlen(std::vector<double> werte)
{
    Kennzahlen k;
    k.n = werte.size();
    if (werte.empty())
        return k;
    std::sort(werte.begin(), werte.end());
    auto q = [&](double p) {
        const double i = p * double(werte.size() - 1);
        const size_t a = size_t(std::floor(i));
        const size_t b = std::min(a + 1, werte.size() - 1);
        return werte[a] + (werte[b] - werte[a]) * (i - double(a));
    };
    k.min = werte.front();
    k.p5 = q(0.05);
    k.median = q(0.5);
    k.p95 = q(0.95);
    k.max = werte.back();
    return k;
}

QString kennzahlenText(const Kennzahlen &k, const char *einheit)
{
    if (k.n == 0)
        return QStringLiteral("n=0");
    const QString e = QString::fromUtf8(einheit);
    return QStringLiteral("n=%1 min %2 p5 %3 Median %4 p95 %5 max %6 %7 (Spanne %8 %7)")
        .arg(k.n)
        .arg(k.min, 0, 'f', 3)
        .arg(k.p5, 0, 'f', 3)
        .arg(k.median, 0, 'f', 3)
        .arg(k.p95, 0, 'f', 3)
        .arg(k.max, 0, 'f', 3)
        .arg(e)
        .arg(k.max - k.min, 0, 'f', 3);
}

namespace {

struct Abschnitt
{
    std::vector<double> x, y;
    double dauer = 0;
    int lauf = 0;
};

// Gemeinsame Steigung über alle Abschnitte, jeder Abschnitt mit eigener Lage (Achsenabschnitt).
// Gibt Steigung, Lagen und Abweichungen (y − Gerade) zurück.
struct Ergebnis
{
    double steigung = 1;
    std::vector<double> lagen;
    std::vector<double> abweichungen;
};

Ergebnis ausgleich(const std::vector<Abschnitt> &abschnitte)
{
    Ergebnis e;
    double sxy = 0, sxx = 0;
    std::vector<double> mx, my;
    for (const Abschnitt &a : abschnitte) {
        double x0 = 0, y0 = 0;
        for (size_t i = 0; i < a.x.size(); ++i) {
            x0 += a.x[i];
            y0 += a.y[i];
        }
        x0 /= double(a.x.size());
        y0 /= double(a.y.size());
        mx.push_back(x0);
        my.push_back(y0);
        for (size_t i = 0; i < a.x.size(); ++i) {
            sxy += (a.x[i] - x0) * (a.y[i] - y0);
            sxx += (a.x[i] - x0) * (a.x[i] - x0);
        }
    }
    if (sxx > 0)
        e.steigung = sxy / sxx;
    for (size_t s = 0; s < abschnitte.size(); ++s) {
        const double lage = my[s] - e.steigung * mx[s];
        e.lagen.push_back(lage);
        for (size_t i = 0; i < abschnitte[s].x.size(); ++i)
            e.abweichungen.push_back(abschnitte[s].y[i] - (lage + e.steigung * abschnitte[s].x[i]));
    }
    return e;
}

} // namespace

QString seitenAuswertung(const std::vector<BildMeldung> &meldungen)
{
    QString t;
    if (meldungen.size() < 20)
        return QStringLiteral("zu wenige Meldungen «bild»: %1\n").arg(meldungen.size());

    // Abschnitte: je Start und Wiederholung
    std::map<std::pair<int, int>, std::vector<const BildMeldung *>> gruppen;
    for (const BildMeldung &m : meldungen)
        gruppen[{m.lauf, m.schleife}].push_back(&m);

    const double dauerS = (meldungen.back().ankunftNs - meldungen.front().ankunftNs) / 1e9;
    t += QStringLiteral("Meldungen «bild»: %1 in %2 s (%3 je s), Abschnitte (Start, Wiederholung): %4\n")
             .arg(meldungen.size())
             .arg(dauerS, 0, 'f', 1)
             .arg(meldungen.size() / std::max(dauerS, 1e-9), 0, 'f', 2)
             .arg(gruppen.size());

    // Lücken in presentedFrames innerhalb eines Starts
    int64_t luecken = 0, groessteLuecke = 0;
    for (size_t i = 1; i < meldungen.size(); ++i) {
        if (meldungen[i].lauf != meldungen[i - 1].lauf)
            continue;
        const int64_t d = meldungen[i].presentedFrames - meldungen[i - 1].presentedFrames;
        if (d > 1) {
            luecken += d - 1;
            groessteLuecke = std::max(groessteLuecke, d - 1);
        }
    }
    t += QStringLiteral("Ausgelassene Bilder (presentedFrames): %1, grösste Lücke %2\n")
             .arg(luecken)
             .arg(groessteLuecke);

    struct Reihe
    {
        const char *name;
        double (*wert)(const BildMeldung &);
    };
    const Reihe reihen[] = {
        {"Eintreffen im Prototyp (steady_clock)", [](const BildMeldung &m) { return m.ankunftNs / 1e9; }},
        {"performance.now() beim Senden", [](const BildMeldung &m) { return m.now / 1e3; }},
        {"presentationTime", [](const BildMeldung &m) { return m.presentationTime / 1e3; }},
        {"expectedDisplayTime", [](const BildMeldung &m) { return m.expectedDisplayTime / 1e3; }},
    };
    for (const Reihe &r : reihen) {
        std::vector<Abschnitt> abschnitte;
        for (const auto &[schluessel, liste] : gruppen) {
            if (liste.size() < 10)
                continue;
            Abschnitt a;
            a.lauf = schluessel.first;
            a.dauer = liste.front()->dauer;
            for (const BildMeldung *m : liste) {
                a.x.push_back(m->mediaTime);
                a.y.push_back(r.wert(*m));
            }
            abschnitte.push_back(std::move(a));
        }
        if (abschnitte.empty())
            continue;
        const Ergebnis e = ausgleich(abschnitte);
        std::vector<double> abw;
        for (double a : e.abweichungen)
            abw.push_back(std::fabs(a) * 1e3);
        const double ppm = (e.steigung - 1.0) * 1e6;
        t += QStringLiteral("\n%1 gegen mediaTime:\n").arg(QString::fromUtf8(r.name));
        t += QStringLiteral("  Gang: %1 ppm = %2 ms/min (Uhr läuft gegen die Medienzeit %3)\n")
                 .arg(ppm, 0, 'f', 2)
                 .arg(ppm * 60e-3, 0, 'f', 3)
                 .arg(ppm >= 0 ? QStringLiteral("schneller") : QStringLiteral("langsamer"));
        t += QStringLiteral("  Schwankung |Abweichung von der Geraden|: %1\n")
                 .arg(kennzahlenText(kennzahlen(abw), "ms"));
        // Sprung beim Wiederholen: Lage des nächsten Abschnitts gegen Lage + Länge des Videos
        std::vector<double> spruenge;
        for (size_t s = 1; s < abschnitte.size(); ++s) {
            if (abschnitte[s].lauf != abschnitte[s - 1].lauf || abschnitte[s - 1].dauer <= 0)
                continue;
            spruenge.push_back((e.lagen[s] - e.lagen[s - 1] - abschnitte[s - 1].dauer * e.steigung) * 1e3);
        }
        if (!spruenge.empty())
            t += QStringLiteral("  Sprung beim Wiederholen (Pause zwischen Ende und Anfang): %1\n")
                     .arg(kennzahlenText(kennzahlen(spruenge), "ms"));
    }

    // Weg von der Seite zum Prototyp: Eintreffen − performance.now(), bezogen auf das Minimum
    std::vector<double> weg;
    double minWeg = 1e300;
    for (const BildMeldung &m : meldungen)
        minWeg = std::min(minWeg, m.ankunftNs / 1e6 - m.now);
    for (const BildMeldung &m : meldungen)
        weg.push_back(m.ankunftNs / 1e6 - m.now - minWeg);
    t += QStringLiteral("\nWeg Seite → Prototyp über dem kürzesten (enthält auch den Gang): %1\n")
             .arg(kennzahlenText(kennzahlen(weg), "ms"));
    return t;
}
