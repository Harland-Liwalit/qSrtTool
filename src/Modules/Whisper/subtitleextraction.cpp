#include "subtitleextraction.h"
#include "ui_subtitleextraction.h"

#include <QFileInfo>
#include <QTimer>
#include <QToolButton>
#include <QTransform>

#include "../../Core/dependencymanager.h"

SubtitleExtraction::SubtitleExtraction(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::SubtitleExtraction)
{
    ui->setupUi(this);

    m_toolsBaseIcon = ui->toolsCheckButton->icon();
    m_toolsSpinTimer = new QTimer(this);
    m_toolsSpinTimer->setInterval(60);
    connect(m_toolsSpinTimer, &QTimer::timeout, this, &SubtitleExtraction::updateToolsSpinner);

    connect(ui->toolsCheckButton, &QToolButton::clicked, this, []() {
        DependencyManager::instance().checkForUpdates();
    });

    auto &manager = DependencyManager::instance();
    connect(&manager, &DependencyManager::busyChanged, this, &SubtitleExtraction::setToolsLoading);
}

SubtitleExtraction::~SubtitleExtraction()
{
    delete ui;
}

/// @brief 加载视频文件到字幕提取界面
/// @details 将视频路径设置到输入框中，供后续字幕提取使用
void SubtitleExtraction::loadVideoFile(const QString &videoPath)
{
    if (!ui || !ui->inputLineEdit) {
        return;
    }

    if (videoPath.isEmpty() || !QFileInfo::exists(videoPath)) {
        return;
    }

    ui->inputLineEdit->setText(videoPath);
}

void SubtitleExtraction::setToolsLoading(bool loading)
{
    if (m_toolsLoading == loading) {
        return;
    }

    m_toolsLoading = loading;
    ui->toolsCheckButton->setEnabled(!loading);

    if (loading) {
        m_toolsSpinAngle = 0;
        m_toolsSpinTimer->start();
    } else {
        m_toolsSpinTimer->stop();
        ui->toolsCheckButton->setIcon(m_toolsBaseIcon);
    }
}

void SubtitleExtraction::updateToolsSpinner()
{
    const QSize iconSize = ui->toolsCheckButton->iconSize();
    QPixmap pixmap = m_toolsBaseIcon.pixmap(iconSize);
    QTransform transform;
    transform.rotate(m_toolsSpinAngle);
    ui->toolsCheckButton->setIcon(QIcon(pixmap.transformed(transform, Qt::SmoothTransformation)));
    m_toolsSpinAngle = (m_toolsSpinAngle + 30) % 360;
}
