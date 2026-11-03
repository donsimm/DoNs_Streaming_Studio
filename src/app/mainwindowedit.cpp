// Bearbeiten im Fenster (P08, Teile C bis E): verbindet Knöpfe, Menüs und Felder mit
// Änderungen an den Kern. Gerechnet wird in dss_common (windowedit.h, overlaylist.h); hier wird
// nur gefragt, geschickt und die Antwort gezeigt (CLAUDE.md Regel 9).

#include "mainwindow.h"

#include "canvaswidget.h"

#include "coreclient.h"
#include "dialogs.h"
#include "dockviews.h"
#include "windowedit.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QLineEdit>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QToolButton>

#ifndef DSS_APP_EDITING
#define DSS_APP_EDITING 1
#endif

namespace {

dss::ChangeRequest request(const char *type)
{
    dss::ChangeRequest r;
    r.type = QString::fromLatin1(type);
    return r;
}

} // namespace

bool MainWindow::editingAllowed() const
{
    return DSS_APP_EDITING && m_client;
}

qint64 MainWindow::sendChange(const dss::ChangeRequest &request, const QString &coalesceKey)
{
    const qint64 id = m_client->sendChange(request, coalesceKey);
    if (id == 0)
        showStatus(QStringLiteral("Keine Verbindung zum Kern, Änderung nicht geschickt"));
    return id;
}

void MainWindow::onChangeFinished(const dss::ChangeResult &result)
{
    clearFinishedPreview();
    const bool selectNew = m_selectOnResult.contains(result.requestId);
    const SelectNew select = m_selectOnResult.take(result.requestId);
    if (!result.ok) {
        showStatus(result.message);
    } else if (!result.warning.isEmpty()) {
        // Zum Beispiel: Grösse der Datei nicht lesbar, Grösse des Overlays genommen (P09).
        showStatus(result.warning);
    }
    if (result.ok && selectNew && !result.id.isNull()) {
        // Neues oder kopiertes Overlay bzw. neue Quelle wählen. Liste und Overlay sind schon da:
        // Der Kern schickt sie vor der Antwort (Teil A).
        if (select == SelectNew::Overlay) {
            selectOverlay(result.id);
        } else {
            m_selectedSource = result.id;
            refreshOverlay();
            return;
        }
    }
    // Felder nachführen, die während der Änderung nicht gesetzt wurden.
    refreshProperties();
}

void MainWindow::selectOverlay(const QUuid &id)
{
    for (QTreeWidgetItemIterator it(m_overlaysDock.tree); *it; ++it) {
        if (!(*it)->data(0, OverlayTree::kFolderRole).toBool()
            && (*it)->data(0, OverlayTree::kIdRole).toUuid() == id) {
            m_overlaysDock.tree->setCurrentItem(*it);
            return;
        }
    }
}

void MainWindow::connectEditing()
{
    // Ziehen und Pfeiltasten auf der Leinwand (P09, Teil C)
    connect(m_canvas, &CanvasWidget::geometryEdited, this, &MainWindow::onCanvasGeometryEdited);

    // Overlays (Teil C)
    OverlayTree *tree = m_overlaysDock.tree;
    connect(m_overlaysDock.add, &QToolButton::clicked, this, &MainWindow::addOverlay);
    connect(m_overlaysDock.remove, &QToolButton::clicked, this,
            [this] { removeOverlay(m_selectedOverlay); });
    connect(m_overlaysDock.copy, &QToolButton::clicked, this,
            [this] { copyOverlay(m_selectedOverlay); });
    connect(tree, &QWidget::customContextMenuRequested, this, &MainWindow::showOverlayMenu);
    connect(tree, &OverlayTree::itemDropped, this, &MainWindow::onOverlayDropped);
    connect(tree, &QTreeWidget::itemExpanded, this,
            [this](QTreeWidgetItem *item) { onFolderExpanded(item, true); });
    connect(tree, &QTreeWidget::itemCollapsed, this,
            [this](QTreeWidgetItem *item) { onFolderExpanded(item, false); });
    connect(tree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *item) {
        const QUuid id = item->data(0, OverlayTree::kIdRole).toUuid();
        if (item->data(0, OverlayTree::kFolderRole).toBool())
            renameFolder(id);
        else
            renameOverlay(id);
    });

    // Quellen (Teil D)
    SourceList *list = m_sourcesDock.list;
    connect(m_sourcesDock.addImage, &QAction::triggered, this, [this] { addMediaSource(false); });
    connect(m_sourcesDock.addVideo, &QAction::triggered, this, [this] { addMediaSource(true); });
    connect(m_sourcesDock.addText, &QAction::triggered, this, &MainWindow::addTextSource);
    connect(m_sourcesDock.remove, &QToolButton::clicked, this,
            [this] { removeSource(m_selectedSource); });
    connect(m_sourcesDock.forward, &QToolButton::clicked, this,
            [this] { stepSource(m_selectedSource, +1); });
    connect(m_sourcesDock.backward, &QToolButton::clicked, this,
            [this] { stepSource(m_selectedSource, -1); });
    connect(list, &SourceList::visibilityClicked, this, &MainWindow::toggleVisibility);
    connect(list, &SourceList::itemDropped, this, &MainWindow::onSourceDropped);
    connect(list, &QWidget::customContextMenuRequested, this, &MainWindow::showSourceMenu);
    connect(list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        renameSource(item->data(SourceList::kIdRole).toUuid());
    });

    // Eigenschaften (Teil E). Zahlenfelder melden valueChanged auch mit den Pfeilen; jede
    // Änderung geht mit eigenem Schlüssel, damit höchstens eine je Feld unterwegs ist.
    PropertiesDock &d = m_propertiesDock;
    connect(d.name, &QLineEdit::editingFinished, this, &MainWindow::onNameEdited);
    const auto pixel = [this](QSpinBox *spin, std::optional<int> dss::SourceChanges::*field,
                              const QString &key) {
        connect(spin, &QSpinBox::valueChanged, this, [this, field, key](int value) {
            dss::SourceChanges c;
            c.*field = value;
            updateSelectedSource(c, key);
        });
    };
    pixel(d.x, &dss::SourceChanges::x, QStringLiteral("x"));
    pixel(d.y, &dss::SourceChanges::y, QStringLiteral("y"));
    pixel(d.width, &dss::SourceChanges::width, QStringLiteral("width"));
    pixel(d.height, &dss::SourceChanges::height, QStringLiteral("height"));
    pixel(d.fontSize, &dss::SourceChanges::fontSize, QStringLiteral("fontSize"));
    connect(d.rotation, &QDoubleSpinBox::valueChanged, this, [this](double degrees) {
        dss::SourceChanges c;
        c.rotation = dss::rotationTenths(degrees);
        updateSelectedSource(c, QStringLiteral("rotation"));
    });
    connect(d.content, &QPlainTextEdit::textChanged, this, [this] {
        dss::SourceChanges c;
        c.content = m_propertiesDock.content->toPlainText();
        updateSelectedSource(c, QStringLiteral("content"));
    });
    connect(d.align, &QComboBox::activated, this, [this](int index) {
        dss::SourceChanges c;
        c.align = dss::TextAlign(m_propertiesDock.align->itemData(index).toInt());
        updateSelectedSource(c, QStringLiteral("align"));
    });
    connect(d.loop, &QCheckBox::clicked, this, [this](bool checked) {
        dss::SourceChanges c;
        c.loop = checked;
        updateSelectedSource(c, QStringLiteral("loop"));
    });
    connect(d.holdAtEnd, &QCheckBox::clicked, this, [this](bool checked) {
        dss::SourceChanges c;
        c.holdAtEnd = checked;
        updateSelectedSource(c, QStringLiteral("holdAtEnd"));
    });

    // Auflösung des Overlays im Dock «Steuerung» (P09, Entscheid 5)
    connect(m_controlDock.overlayWidth, &QSpinBox::valueChanged, this,
            &MainWindow::onOverlaySizeEdited);
    connect(m_controlDock.overlayHeight, &QSpinBox::valueChanged, this,
            &MainWindow::onOverlaySizeEdited);
    connect(d.family, &QComboBox::activated, this, &MainWindow::onFamilyChosen);
    connect(d.weight, &QComboBox::activated, this, &MainWindow::onFontStyleChosen);
    connect(d.italic, &QCheckBox::clicked, this, &MainWindow::onFontStyleChosen);
    connect(d.color, &QToolButton::clicked, this, &MainWindow::chooseColor);
    connect(d.replaceImage, &QPushButton::clicked, this, &MainWindow::replaceImage);

    connect(m_client, &dss::CoreClient::changeFinished, this, &MainWindow::onChangeFinished);
    connect(m_client, &dss::CoreClient::fontsChanged, this, &MainWindow::refreshFonts);
}

// --- Overlays (Teil C) ---

void MainWindow::addOverlay()
{
    QString name;
    int width = 0;
    int height = 0;
    if (!askNewOverlay(this, &name, &width, &height))
        return;
    dss::ChangeRequest r = request(dss::msg::kCreateOverlay);
    r.name = name;
    r.width = width;
    r.height = height;
    if (const qint64 id = sendChange(r); id > 0)
        m_selectOnResult.insert(id, SelectNew::Overlay);
}

void MainWindow::renameOverlay(const QUuid &id)
{
    // Kopie: Während der Dialog offen ist, kann eine neue Liste kommen.
    const dss::OverlaySummary *found = m_client->listData().summary(id);
    if (!found)
        return;
    const dss::OverlaySummary overlay = *found;
    const dss::OverlaySummary *o = &overlay;
    QString name = o->name;
    if (!askText(this, QStringLiteral("Overlay umbenennen"), QStringLiteral("Neuer Name"), &name)
        || name == o->name)
        return;
    dss::ChangeRequest r = request(dss::msg::kRenameOverlay);
    r.overlayId = id;
    r.name = name;
    // Adresse nur auf Nachfrage ändern, Standard «nein» (Entscheid 7): Die Browserquelle in OBS
    // zeigt sonst ins Leere.
    QString newAddress;
    if (dss::renameChangesAddress(m_client->overlays(), id, name, &newAddress)) {
        r.changeAddress = confirm(
            this, QStringLiteral("Adresse ändern?"),
            QStringLiteral("Soll sich auch die Adresse der Browserquelle ändern?\n\n"
                           "Bisher: %1\nNeu: %2\n\n"
                           "Mit einer neuen Adresse muss die Browserquelle in OBS angepasst werden.")
                .arg(o->address, newAddress),
            QStringLiteral("Adresse ändern"), QStringLiteral("Adresse behalten"));
    }
    sendChange(r);
}

void MainWindow::copyOverlay(const QUuid &id)
{
    if (id.isNull())
        return;
    dss::ChangeRequest r = request(dss::msg::kCopyOverlay);
    r.overlayId = id;
    if (const qint64 requestId = sendChange(r); requestId > 0)
        m_selectOnResult.insert(requestId, SelectNew::Overlay);
}

void MainWindow::removeOverlay(const QUuid &id)
{
    // Kopie: Während der Dialog offen ist, kann eine neue Liste kommen.
    const dss::OverlaySummary *found = m_client->listData().summary(id);
    if (!found)
        return;
    const dss::OverlaySummary overlay = *found;
    const dss::OverlaySummary *o = &overlay;
    if (!confirm(this, QStringLiteral("Overlay löschen?"),
                 QStringLiteral("Overlay «%1» mit %2 Quelle(n) löschen?\n\n"
                                "Das lässt sich nicht rückgängig machen. Die Adresse %3 gilt danach nicht mehr.")
                     .arg(o->name).arg(o->sourceCount).arg(o->address),
                 QStringLiteral("Löschen"), QStringLiteral("Abbrechen")))
        return;
    dss::ChangeRequest r = request(dss::msg::kRemoveOverlay);
    r.overlayId = id;
    sendChange(r);
}

void MainWindow::createFolder()
{
    QString name = dss::newFolderName();
    if (!askText(this, QStringLiteral("Ordner anlegen"), QStringLiteral("Name des Ordners"), &name))
        return;
    dss::ChangeRequest r = request(dss::msg::kCreateFolder);
    r.name = name;
    sendChange(r);
}

void MainWindow::renameFolder(const QUuid &id)
{
    // Kopie: Während der Dialog offen ist, kann eine neue Liste kommen.
    const dss::ListFolder *found = m_client->listData().layout.folder(id);
    if (!found)
        return;
    const dss::ListFolder copy = *found;
    const dss::ListFolder *folder = &copy;
    QString name = folder->name;
    if (!askText(this, QStringLiteral("Ordner umbenennen"), QStringLiteral("Neuer Name"), &name)
        || name == folder->name)
        return;
    dss::ChangeRequest r = request(dss::msg::kRenameFolder);
    r.folderId = id;
    r.name = name;
    sendChange(r);
}

void MainWindow::removeFolder(const QUuid &id)
{
    // Kopie: Während der Dialog offen ist, kann eine neue Liste kommen.
    const dss::ListFolder *found = m_client->listData().layout.folder(id);
    if (!found)
        return;
    const dss::ListFolder copy = *found;
    const dss::ListFolder *folder = &copy;
    if (!confirm(this, QStringLiteral("Ordner löschen?"),
                 QStringLiteral("Ordner «%1» löschen?\n\nDie %2 Overlay(s) darin bleiben erhalten "
                                "und kommen auf die oberste Ebene.")
                     .arg(folder->name).arg(folder->overlays.size()),
                 QStringLiteral("Löschen"), QStringLiteral("Abbrechen")))
        return;
    dss::ChangeRequest r = request(dss::msg::kRemoveFolder);
    r.folderId = id;
    sendChange(r);
}

void MainWindow::showOverlayMenu(const QPoint &position)
{
    if (!m_client->isConnected())
        return;
    const QTreeWidgetItem *item = m_overlaysDock.tree->itemAt(position);
    const QUuid id = item ? item->data(0, OverlayTree::kIdRole).toUuid() : QUuid();
    const bool folder = item && item->data(0, OverlayTree::kFolderRole).toBool();
    QMenu menu(this);
    if (item && !folder) {
        menu.addAction(QStringLiteral("Umbenennen …"), this, [this, id] { renameOverlay(id); });
        menu.addAction(QStringLiteral("Kopieren"), this, [this, id] { copyOverlay(id); });
        menu.addAction(QStringLiteral("Löschen …"), this, [this, id] { removeOverlay(id); });
        menu.addSeparator();
    } else if (folder) {
        menu.addAction(QStringLiteral("Ordner umbenennen …"), this, [this, id] { renameFolder(id); });
        menu.addAction(QStringLiteral("Ordner löschen …"), this, [this, id] { removeFolder(id); });
        menu.addSeparator();
    }
    menu.addAction(QStringLiteral("Overlay anlegen …"), this, &MainWindow::addOverlay);
    menu.addAction(QStringLiteral("Ordner anlegen …"), this, &MainWindow::createFolder);
    menu.exec(m_overlaysDock.tree->viewport()->mapToGlobal(position));
}

void MainWindow::onOverlayDropped(const QUuid &id, bool isFolder, const QUuid &target,
                                  bool targetIsFolder, dss::DropPosition position)
{
    const dss::OverlayLayout &layout = m_client->listData().layout;
    int index = 0;
    if (isFolder) {
        if (!dss::folderDropTarget(layout, id, target, targetIsFolder, position, &index))
            return;
        dss::ChangeRequest r = request(dss::msg::kMoveFolder);
        r.folderId = id;
        r.index = index;
        sendChange(r);
        return;
    }
    QUuid folderId;
    if (!dss::overlayDropTarget(layout, id, target, targetIsFolder, position, &folderId, &index))
        return;
    dss::ChangeRequest r = request(dss::msg::kMoveOverlay);
    r.overlayId = id;
    r.folderId = folderId;
    r.index = index;
    sendChange(r);
}

void MainWindow::onFolderExpanded(QTreeWidgetItem *item, bool expanded)
{
    // Auf- und Zuklappen wird gespeichert (liste.json), nicht beim Füllen des Baums.
    if (m_filling || !item->data(0, OverlayTree::kFolderRole).toBool())
        return;
    const dss::ListFolder *folder =
        m_client->listData().layout.folder(item->data(0, OverlayTree::kIdRole).toUuid());
    if (!folder || folder->expanded == expanded)
        return;
    dss::ChangeRequest r = request(dss::msg::kSetFolderExpanded);
    r.folderId = folder->id;
    r.expanded = expanded;
    sendChange(r);
}

// --- Quellen (Teil D) ---

QString MainWindow::askMediaFile(const QString &title, const QString &filter)
{
    const QString file = QFileDialog::getOpenFileName(this, title, m_lastMediaDir, filter);
    if (!file.isEmpty())
        m_lastMediaDir = QFileInfo(file).absolutePath();
    return file;
}

void MainWindow::addMediaSource(bool video)
{
    if (m_selectedOverlay.isNull())
        return;
    // Der Kern kopiert die Datei in den Datenordner (K-03).
    const QString file =
        askMediaFile(video ? QStringLiteral("Video wählen") : QStringLiteral("Bild wählen"),
                     dss::mediaFileFilter(video ? dss::MediaUse::Video : dss::MediaUse::Image));
    if (file.isEmpty())
        return;
    dss::ChangeRequest r = request(video ? dss::msg::kAddVideoSource : dss::msg::kAddImageSource);
    r.overlayId = m_selectedOverlay;
    r.file = file;
    if (const qint64 id = sendChange(r); id > 0)
        m_selectOnResult.insert(id, SelectNew::Source);
}

void MainWindow::addTextSource()
{
    if (m_selectedOverlay.isNull())
        return;
    dss::ChangeRequest r = request(dss::msg::kAddTextSource);
    r.overlayId = m_selectedOverlay;
    if (const qint64 id = sendChange(r); id > 0)
        m_selectOnResult.insert(id, SelectNew::Source);
}

void MainWindow::removeSource(const QUuid &id)
{
    const std::optional<dss::Overlay> overlay = selectedOverlay();
    const dss::Source *source = overlay ? dss::findSource(*overlay, id) : nullptr;
    if (!source)
        return;
    if (!confirm(this, QStringLiteral("Quelle entfernen?"),
                 QStringLiteral("Quelle «%1» aus dem Overlay «%2» entfernen?")
                     .arg(source->name, overlay->name),
                 QStringLiteral("Entfernen"), QStringLiteral("Abbrechen")))
        return;
    dss::ChangeRequest r = request(dss::msg::kRemoveSource);
    r.overlayId = overlay->id;
    r.sourceId = id;
    sendChange(r);
}

void MainWindow::renameSource(const QUuid &id)
{
    const std::optional<dss::Overlay> overlay = selectedOverlay();
    const dss::Source *source = overlay ? dss::findSource(*overlay, id) : nullptr;
    if (!source)
        return;
    QString name = source->name;
    if (!askText(this, QStringLiteral("Quelle umbenennen"), QStringLiteral("Neuer Name"), &name)
        || name == source->name)
        return;
    dss::SourceChanges c;
    c.name = name;
    sendChange(dss::sourceUpdate(overlay->id, id, c));
}

void MainWindow::stepSource(const QUuid &id, int step)
{
    const std::optional<dss::Overlay> overlay = selectedOverlay();
    if (!overlay)
        return;
    const std::optional<int> index = dss::sourceStepIndex(*overlay, id, step);
    if (!index)
        return;
    dss::ChangeRequest r = request(dss::msg::kMoveSource);
    r.overlayId = overlay->id;
    r.sourceId = id;
    r.index = *index;
    sendChange(r);
}

void MainWindow::toggleVisibility(const QUuid &id)
{
    const std::optional<dss::Overlay> overlay = selectedOverlay();
    const dss::Source *source = overlay ? dss::findSource(*overlay, id) : nullptr;
    if (!source || !m_client->isConnected())
        return;
    dss::SourceChanges c;
    c.visible = !source->visible;
    sendChange(dss::sourceUpdate(overlay->id, id, c), dss::changeKey(id, QStringLiteral("visible")));
}

void MainWindow::showSourceMenu(const QPoint &position)
{
    const QListWidgetItem *item = m_sourcesDock.list->itemAt(position);
    const std::optional<dss::Overlay> overlay = selectedOverlay();
    if (!m_client->isConnected() || !overlay)
        return;
    QMenu menu(this);
    if (item) {
        const QUuid id = item->data(SourceList::kIdRole).toUuid();
        const dss::Source *source = dss::findSource(*overlay, id);
        if (!source)
            return;
        menu.addAction(QStringLiteral("Umbenennen …"), this, [this, id] { renameSource(id); });
        menu.addAction(source->visible ? QStringLiteral("Ausblenden") : QStringLiteral("Einblenden"),
                       this, [this, id] { toggleVisibility(id); });
        QAction *forward =
            menu.addAction(QStringLiteral("Nach vorne"), this, [this, id] { stepSource(id, +1); });
        forward->setEnabled(dss::sourceStepIndex(*overlay, id, +1).has_value());
        QAction *backward =
            menu.addAction(QStringLiteral("Nach hinten"), this, [this, id] { stepSource(id, -1); });
        backward->setEnabled(dss::sourceStepIndex(*overlay, id, -1).has_value());
        menu.addAction(QStringLiteral("Entfernen …"), this, [this, id] { removeSource(id); });
        menu.addSeparator();
    }
    menu.addAction(QStringLiteral("Bild hinzufügen …"), this, [this] { addMediaSource(false); });
    menu.addAction(QStringLiteral("Video hinzufügen …"), this, [this] { addMediaSource(true); });
    menu.addAction(QStringLiteral("Text hinzufügen"), this, &MainWindow::addTextSource);
    menu.exec(m_sourcesDock.list->viewport()->mapToGlobal(position));
}

void MainWindow::onSourceDropped(const QUuid &id, int insertRow)
{
    const std::optional<dss::Overlay> overlay = selectedOverlay();
    if (!overlay)
        return;
    const std::optional<int> index = dss::sourceDropIndex(*overlay, id, insertRow);
    if (!index)
        return;
    dss::ChangeRequest r = request(dss::msg::kMoveSource);
    r.overlayId = overlay->id;
    r.sourceId = id;
    r.index = *index;
    sendChange(r);
}

// --- Eigenschaften (Teil E) ---

void MainWindow::updateSelectedSource(const dss::SourceChanges &changes, const QString &field)
{
    if (m_selectedOverlay.isNull() || m_selectedSource.isNull())
        return;
    sendChange(dss::sourceUpdate(m_selectedOverlay, m_selectedSource, changes),
               dss::changeKey(m_selectedSource, field));
}

void MainWindow::onOverlaySizeEdited()
{
    if (m_selectedOverlay.isNull())
        return;
    dss::ChangeRequest r = request(dss::msg::kSetOverlaySize);
    r.overlayId = m_selectedOverlay;
    r.width = m_controlDock.overlayWidth->value();
    r.height = m_controlDock.overlayHeight->value();
    sendChange(r, dss::overlaySizeKey(m_selectedOverlay));
}

void MainWindow::onCanvasSourceClicked(const QUuid &id)
{
    SourceList *list = m_sourcesDock.list;
    for (int row = 0; row < list->count(); ++row) {
        if (list->item(row)->data(SourceList::kIdRole).toUuid() == id) {
            list->setCurrentRow(row);
            return;
        }
    }
    list->setCurrentItem(nullptr); // Klick daneben: Auswahl aufheben
}

void MainWindow::onCanvasGeometryEdited(const QUuid &id, const dss::SourceGeometry &geometry,
                                        bool finished)
{
    if (m_selectedOverlay.isNull())
        return;
    m_geometryPreview = GeometryPreview{m_selectedOverlay, id, geometry, !finished};
    // Alle Werte der Transformation in einer Änderung (seit P09b auch Spiegeln); häufige Änderungen
    // fasst CoreClient zusammen (wie P08): unterwegs ist höchstens eine, danach geht die neueste.
    const dss::SourceChanges c = dss::geometryChanges(geometry);
    sendChange(dss::sourceUpdate(m_selectedOverlay, id, c), dss::changeKey(id, QStringLiteral("geometry")));
    // Sofort zeigen: Leinwand und Felder der Eigenschaften (ohne die Liste neu zu bauen).
    m_canvas->setOverlay(selectedOverlay());
    refreshProperties();
}

void MainWindow::clearFinishedPreview()
{
    if (!m_geometryPreview || m_geometryPreview->dragging)
        return;
    if (m_client && m_client->isChangePending(
            dss::changeKey(m_geometryPreview->sourceId, QStringLiteral("geometry"))))
        return;
    m_geometryPreview.reset();
    m_canvas->setOverlay(selectedOverlay());
}

void MainWindow::onNameEdited()
{
    const std::optional<dss::Overlay> overlay = selectedOverlay();
    const dss::Source *source = overlay ? dss::findSource(*overlay, m_selectedSource) : nullptr;
    if (!source)
        return;
    const QString name = m_propertiesDock.name->text();
    if (name == source->name)
        return;
    if (name.trimmed().isEmpty()) {
        // Leerer Name gilt nicht: alten Namen wieder zeigen.
        showStatus(QStringLiteral("Der Name der Quelle darf nicht leer sein."));
        m_propertiesDock.name->setText(source->name);
        return;
    }
    dss::SourceChanges c;
    c.name = name;
    updateSelectedSource(c, QStringLiteral("name"));
}

void MainWindow::onFamilyChosen(int index)
{
    const QString family = m_propertiesDock.family->itemData(index).toString();
    if (family.isEmpty()) {
        // «Datei wählen …»: eigene Schriftdatei; Gewicht und kursiv liest der Kern aus der Datei.
        const QString file = askMediaFile(QStringLiteral("Schriftdatei wählen"),
                                          dss::mediaFileFilter(dss::MediaUse::Font));
        if (!file.isEmpty()) {
            dss::ChangeRequest r = request(dss::msg::kSetTextFont);
            r.overlayId = m_selectedOverlay;
            r.sourceId = m_selectedSource;
            r.file = file;
            sendChange(r);
        }
        refreshProperties(); // Auswahl zurück auf die aktuelle Schrift
        return;
    }
    onFontStyleChosen();
}

void MainWindow::onFontStyleChosen()
{
    const PropertiesDock &d = m_propertiesDock;
    const QString family = d.family->currentData().toString();
    const int weight = d.weight->currentIndex() >= 0 ? d.weight->currentData().toInt() : 400;
    const std::optional<dss::ChangeRequest> r =
        dss::fontChange(m_client->fonts(), m_selectedOverlay, m_selectedSource, family, weight,
                        d.italic->isChecked());
    if (r)
        sendChange(*r);
    else
        refreshProperties();
}

void MainWindow::chooseColor()
{
    const std::optional<dss::Overlay> overlay = selectedOverlay();
    const dss::Source *source = overlay ? dss::findSource(*overlay, m_selectedSource) : nullptr;
    if (!source || source->kind != dss::SourceKind::Text)
        return;
    int r = 255, g = 255, b = 255, a = 255;
    dss::colorParts(source->text.color, &r, &g, &b, &a);
    const QColor color = QColorDialog::getColor(QColor(r, g, b, a), this, QStringLiteral("Textfarbe"),
                                                QColorDialog::ShowAlphaChannel);
    if (!color.isValid())
        return;
    dss::SourceChanges c;
    c.color = dss::colorText(color.red(), color.green(), color.blue(), color.alpha());
    updateSelectedSource(c, QStringLiteral("color"));
}

void MainWindow::replaceImage()
{
    if (m_selectedOverlay.isNull() || m_selectedSource.isNull())
        return;
    const QString file = askMediaFile(QStringLiteral("Neues Bild wählen"),
                                      dss::mediaFileFilter(dss::MediaUse::Image));
    if (file.isEmpty())
        return;
    dss::ChangeRequest r = request(dss::msg::kReplaceImageFile);
    r.overlayId = m_selectedOverlay;
    r.sourceId = m_selectedSource;
    r.file = file;
    sendChange(r);
}
