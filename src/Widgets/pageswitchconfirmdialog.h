#ifndef PAGESWITCHCONFIRMDIALOG_H
#define PAGESWITCHCONFIRMDIALOG_H

#include <QDialog>

class QCheckBox;
class QLabel;
class QPushButton;

/**
 * @class PageSwitchConfirmDialog
 * @brief 切换页面前的任务中断确认弹窗
 *
 * 功能：
 * 1. 在存在运行中任务时提示用户是否切换页面
 * 2. 支持“本次任务期间不再提示”选项
 */
class PageSwitchConfirmDialog : public QDialog
{
    Q_OBJECT

public:
    explicit PageSwitchConfirmDialog(QWidget *parent = nullptr);

    /**
     * @brief 设置目标页面名称
     * @param targetName 目标页面显示名
     */
    void setTargetName(const QString &targetName);

    /**
     * @brief 是否勾选"不再提示"（程序运行期间永久生效）
     * @return 勾选返回true，否则false
     */
    bool skipPromptForCurrentTask() const;

private:
    QLabel *m_messageLabel = nullptr;
    QCheckBox *m_skipPromptCheck = nullptr;
    QPushButton *m_confirmButton = nullptr;
    QPushButton *m_cancelButton = nullptr;
};

#endif // PAGESWITCHCONFIRMDIALOG_H
