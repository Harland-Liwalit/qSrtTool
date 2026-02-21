#ifndef SUBTITLEEXTRACTION_H
#define SUBTITLEEXTRACTION_H

#include <QWidget>
#include <QIcon>

class QTimer;
class QShowEvent;

namespace Ui {
class SubtitleExtraction;
}

class SubtitleExtraction : public QWidget
{
    Q_OBJECT

public:
    explicit SubtitleExtraction(QWidget *parent = nullptr);
    ~SubtitleExtraction();

    /// @brief 加载视频文件到字幕提取界面
    /// @param videoPath 视频文件的绝对路径
    void loadVideoFile(const QString &videoPath);

private:
    Ui::SubtitleExtraction *ui;
    QTimer *m_toolsSpinTimer = nullptr;
    QIcon m_toolsBaseIcon;
    int m_toolsSpinAngle = 0;
    bool m_toolsLoading = false;

    void setToolsLoading(bool loading);
    void updateToolsSpinner();
    QString whisperModelsDirPath() const;
    void ensureModelDirectories();
    void refreshWhisperModelList();
    void openWhisperModelsDirectory();

protected:
    void showEvent(QShowEvent *event) override;
};

#endif // SUBTITLEEXTRACTION_H
