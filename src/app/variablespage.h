#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <QWidget>

class QComboBox;
class QLabel;
class QLineEdit;
class QSplitter;
class QToolButton;
class QTreeWidget;

// P14 e3-7: Unterreiter «Variablen» (Checkliste P14 e3, Abschnitt 3 «Variablen»). «Für alle» und «Je
// Zuschauer» nebeneinander in einem waagrechten QSplitter wie «Live» (Entscheid des Nutzers,
// 2026-11-16), je ein Bereich wie ein Dock mit Suchfeld, Tabelle, Prüfzeile (#fieldProblem) und
// Leiste mit «Variable löschen». Ohne Logik (Regel 9): Zeilen, Filter und Prüfmeldungen kommen vom
// MainWindow aus dss::actionview; ein geänderter Wert meldet nur den Text der Zelle.
class VariablesPage : public QWidget
{
    Q_OBJECT

public:
    enum Area { Global = 0, User, AreaCount };

    struct Row
    {
        QString key;       // Kennung der Zeile (Auswahl bleibt beim Neufüllen)
        QStringList cells; // Spalten der Tabelle, «Wert» an der Stelle valueColumn(area)
    };

    explicit VariablesPage(QWidget *parent = nullptr);

    // Spalte «Wert» der Tabelle (einzige änderbare Zelle)
    static int valueColumn(Area area) { return area == Global ? 1 : 2; }

    // Füllt nur die Zeilen; die Auswahl bleibt auf derselben Kennung. Ist eine Zelle gerade in
    // Bearbeitung, erst nach deren Ende (sonst ginge die Eingabe verloren).
    void setRows(Area area, const QList<Row> &rows);
    QString selectedRow(Area area) const;
    // editable: Werte änderbar (verbunden, geladen, Datei gültig)
    void setEditable(bool editable);
    void setRemoveEnabled(Area area, bool enabled);
    void setProblem(Area area, const QString &text);
    QString problem(Area area) const;
    QString searchText(Area area) const;
    // Filter «Je Zuschauer»: values[0] leer = alle; die Auswahl bleibt, wenn es den Wert noch gibt.
    void setFilterChoices(const QStringList &values, const QStringList &labels);
    QString filterValue() const;

signals:
    void searchChanged(Area area);
    void filterChanged();
    void selectionChanged(Area area);
    void valueEdited(Area area, const QString &key, const QString &text);
    void removeClicked(Area area);

private:
    QWidget *createArea(Area area);
    void fillRows(Area area, const QList<Row> &rows);

    QSplitter *m_splitter = nullptr;
    QLineEdit *m_search[AreaCount] = {};
    QComboBox *m_filter = nullptr;
    QTreeWidget *m_tables[AreaCount] = {};
    QLabel *m_problems[AreaCount] = {};
    QToolButton *m_remove[AreaCount] = {};
    // Zeilen, die während einer offenen Bearbeitung kamen; gefüllt, sobald der Editor zu ist
    QList<Row> m_pendingRows[AreaCount];
    bool m_pending[AreaCount] = {};
    bool m_editable = false;
    bool m_filling = false;
};
