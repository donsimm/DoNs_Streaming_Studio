#pragma once

#include "overlaylist.h"

#include <QIcon>
#include <QListWidget>
#include <QTreeWidget>
#include <QUuid>

// Listen der Docks «Overlays» und «Quellen» mit Ziehen per Maus (P08, Teile C und D). Sie
// verschieben beim Loslassen nichts selbst: Sie melden nur, was wohin gezogen wurde. Den neuen
// Stand zeigt das Fenster erst, wenn der Kern ihn an alle schickt (Teil A).

// Baum der Overlays: Overlays und Ordner auf der obersten Ebene, Overlays in Ordnern.
class OverlayTree : public QTreeWidget
{
    Q_OBJECT

public:
    // Daten eines Eintrags: Kennung und ob es ein Ordner ist.
    static constexpr int kIdRole = Qt::UserRole;
    static constexpr int kFolderRole = Qt::UserRole + 1;
    // P10 (E-24): Ausgabe ein/aus eines Overlays (bool); ohne Wert kein Schalter.
    static constexpr int kOutputRole = Qt::UserRole + 2;

    explicit OverlayTree(QWidget *parent = nullptr);

    // Symbole des Schalters «Ausgabe» (ein, aus); leer = kein Schalter.
    void setOutputIcons(const QIcon &on, const QIcon &off);
    // Fläche des Schalters eines Overlays: das letzte Stück des Einzugs links vom Eintrag
    // (Punkt 48); leer für Ordner und ohne Symbole.
    QRect outputSwitchRect(const QTreeWidgetItem *item) const;

signals:
    // Eintrag id (Overlay oder Ordner) auf target losgelassen; target ist bei EndOfList null.
    void itemDropped(const QUuid &id, bool isFolder, const QUuid &target, bool targetIsFolder,
                     dss::DropPosition position);
    // Klick auf den Schalter «Ausgabe» des Overlays id; die Auswahl bleibt.
    void outputClicked(const QUuid &id);

protected:
    void dropEvent(QDropEvent *event) override;
    void drawBranches(QPainter *painter, const QRect &rect, const QModelIndex &index) const override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
    bool onOutputSwitch(const QPoint &position, QUuid *id) const;

    QIcon m_outputOn;
    QIcon m_outputOff;
};

// Liste der Quellen, vorderste oben. Klick auf das Auge schaltet die Sichtbarkeit um.
class SourceList : public QListWidget
{
    Q_OBJECT

public:
    static constexpr int kIdRole = Qt::UserRole;

    explicit SourceList(QWidget *parent = nullptr);

signals:
    void visibilityClicked(const QUuid &id);
    // Quelle id an der Einfügestelle insertRow losgelassen (0 = über der obersten Zeile).
    void itemDropped(const QUuid &id, int insertRow);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void dropEvent(QDropEvent *event) override;
};
