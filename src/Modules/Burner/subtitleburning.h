#ifndef SUBTITLEBURNING_H
#define SUBTITLEBURNING_H

#include <QWidget>
#include <QIcon>

class QTimer;

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

    void setToolsLoading(bool loading);
    void updateToolsSpinner();
};

#endif // SUBTITLEBURNING_H
