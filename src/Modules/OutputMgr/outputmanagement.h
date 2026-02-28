#ifndef OUTPUTMANAGEMENT_H
#define OUTPUTMANAGEMENT_H

#include <QWidget>
#include <QDate>
#include <QDateTime>

class QShowEvent;
class QFileInfo;

namespace Ui {
class OutputManagement;
}

class OutputManagement : public QWidget
{
    Q_OBJECT

public:
    explicit OutputManagement(QWidget *parent = nullptr);
    ~OutputManagement();

    void refreshOutputs();

protected:
    void showEvent(QShowEvent *event) override;

private slots:
    void applyFilters();
    void onOpenOutputFolderClicked();
    void onExportListClicked();

private:
    struct OutputRecord {
        QString name;
        QString type;
        QString source;
        QString status;
        QDateTime modifiedAt;
        QString path;
    };

    void setupUiState();
    void setupConnections();
    void collectOutputsFromDirectory(const QString &directoryPath,
                                     const QString &type,
                                     const QString &source,
                                     QList<OutputRecord> *records) const;
    bool shouldIncludeFile(const QFileInfo &info) const;
    void populateTable(const QList<OutputRecord> &records);
    QString workspaceOutputRoot() const;

private:
    Ui::OutputManagement *ui;
    QList<OutputRecord> m_allRecords;
    bool m_initialized = false;
};

#endif // OUTPUTMANAGEMENT_H
