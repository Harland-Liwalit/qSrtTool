#ifndef SUBTITLEBURNING_H
#define SUBTITLEBURNING_H

#include <QWidget>
#include <QIcon>
#include <QString>

class QTimer;
class SubtitleBurnTaskRunner;

namespace Ui {
class SubtitleBurning;
}

class SubtitleBurning : public QWidget
{
    Q_OBJECT

public:
    explicit SubtitleBurning(QWidget *parent = nullptr);
    ~SubtitleBurning();

private:
    Ui::SubtitleBurning *ui;
    QTimer *m_toolsSpinTimer = nullptr;
    QIcon m_toolsBaseIcon;
    int m_toolsSpinAngle = 0;
    bool m_toolsLoading = false;
    QString m_inputVideoPath;
    QString m_externalSubtitlePath;
    SubtitleBurnTaskRunner *m_burnTaskRunner = nullptr;

    void setToolsLoading(bool loading);
    void updateToolsSpinner();
    void setupBurnWorkflowUi();
    void updateRunningStateUi(bool running);
    void appendLogLine(const QString &message);
    QString defaultVideoImportDirectory() const;
    QString defaultSubtitleImportDirectory() const;
    void saveLastVideoImportDirectory(const QString &filePath);
    void saveLastSubtitleImportDirectory(const QString &filePath);
    void populateContainerOptions();
    void syncOutputPathExtensionWithContainer();
    QString selectedContainerId() const;

    QString selectedContainerExtension() const;
    QString suggestedOutputPath() const;
    bool startBurnTask();
};

#endif // SUBTITLEBURNING_H
