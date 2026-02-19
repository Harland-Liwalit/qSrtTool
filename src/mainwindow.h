#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

#include <QVector>

class QTimer;
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

    void setupPerformanceCounters();
    void updatePerformanceMetrics();
    void teardownPerformanceCounters();
    void initializeDependencies();
    void triggerDependencyCheckOnce();
    void setStatusHint(const QString &message);

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
