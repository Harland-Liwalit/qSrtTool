#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

#include <QHash>
#include <QVector>

class QTimer;
class QToolButton;
class QWidget;
class VideoLoader;
#ifdef Q_OS_WIN
#include <pdh.h>
#endif

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private:
    Ui::MainWindow *ui;
    QTimer *perfTimer = nullptr;
    bool m_dependencyAutoTriggered = false;
    bool m_skipPromptForCurrentTask = false;
    VideoLoader *m_loaderPage = nullptr;
    QHash<QToolButton *, QWidget *> m_navToPage;
    QHash<QToolButton *, QString> m_navFeatureNames;

    void setupPerformanceCounters();
    void updatePerformanceMetrics();
    void teardownPerformanceCounters();
    void initializeDependencies();
    void triggerDependencyCheckOnce();
    void setStatusHint(const QString &message);

    /// @brief 绑定侧边导航按钮到目标页面
    /// @param button 导航按钮
    /// @param page 目标页面
    /// @param featureName 功能名称（用于确认弹窗文案）
    void bindNavigationButton(QToolButton *button, QWidget *page, const QString &featureName);

    /// @brief 处理页面切换请求（带任务确认与中断逻辑）
    /// @param targetPage 目标页面
    /// @param featureName 功能名称
    void requestPageSwitch(QWidget *targetPage, const QString &featureName);

    /// @brief 判断指定页面是否有运行中的任务
    /// @param page 页面对象
    /// @return 有任务返回true，否则false
    bool hasActiveTasksOnPage(QWidget *page) const;

    /// @brief 停止指定页面上所有可停止任务
    /// @param page 页面对象
    void stopAllTasksOnPage(QWidget *page);

    /// @brief 根据当前页面同步左侧导航按钮选中状态
    /// @param page 当前页面
    void syncNavigationSelection(QWidget *page);

#ifdef Q_OS_WIN
    PDH_HQUERY perfQuery = nullptr;
    PDH_HCOUNTER cpuCounter = nullptr;
    PDH_HCOUNTER ramCounter = nullptr;
    QVector<PDH_HCOUNTER> gpuCounters;
    QVector<PDH_HCOUNTER> vramUsageCounters;
    QVector<PDH_HCOUNTER> vramLimitCounters;
#endif
};
#endif // MAINWINDOW_H
