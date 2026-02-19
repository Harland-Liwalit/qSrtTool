#ifndef SUBTITLEEXTRACTION_H
#define SUBTITLEEXTRACTION_H

#include <QWidget>
#include <QIcon>

class QTimer;

namespace Ui {
class SubtitleExtraction;
}

class SubtitleExtraction : public QWidget
{
    Q_OBJECT

public:
    explicit SubtitleExtraction(QWidget *parent = nullptr);
    ~SubtitleExtraction();

private:
    Ui::SubtitleExtraction *ui;
    QTimer *m_toolsSpinTimer = nullptr;
    QIcon m_toolsBaseIcon;
    int m_toolsSpinAngle = 0;
    bool m_toolsLoading = false;

    void setToolsLoading(bool loading);
    void updateToolsSpinner();
};

#endif // SUBTITLEEXTRACTION_H
