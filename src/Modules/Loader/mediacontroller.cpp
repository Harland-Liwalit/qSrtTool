#include "mediacontroller.h"

#include <QFileInfo>
#include <QMediaPlayer>
#include <QProcess>
#include <QStringList>
#include <QUrl>
#include <QVideoWidget>
#include <QDir>
#include <QDirIterator>

MediaController::MediaController(QObject *parent)
	: QObject(parent)
{
	m_player = new QMediaPlayer(this);
	m_ffplayProcess = new QProcess(this);
	connect(m_player, QOverload<QMediaPlayer::Error>::of(&QMediaPlayer::error), this, [this](QMediaPlayer::Error) {
		QString message = m_player ? m_player->errorString().trimmed() : QString();
		if (message.isEmpty()) {
#ifdef Q_OS_WIN
			message = tr("播放器无法渲染该视频，可能缺少解码器。请尝试安装对应的解码器/分离器。");
#else
			message = tr("播放器无法渲染该视频。请检查编码或安装解码器。");
#endif
		}
		emit mediaLoadFailed(message);
		emit mediaStatusMessage(tr("加载失败"));
	});
}

void MediaController::setUseFfplay(bool enabled)
{
	m_useFfplay = enabled;
}

void MediaController::setVideoOutput(QVideoWidget *videoWidget)
{
	if (!m_player) {
		return;
	}

	m_player->setVideoOutput(videoWidget);
}

bool MediaController::loadVideo(const QString &filePath)
{
	if (!m_player) {
		emit mediaLoadFailed(tr("播放器未初始化"));
		return false;
	}

	if (!isVideoFile(filePath)) {
		emit mediaLoadFailed(tr("不是有效的视频文件"));
		return false;
	}

	m_currentFilePath = QFileInfo(filePath).absoluteFilePath();

	emit mediaStatusMessage(tr("正在加载视频..."));

	if (m_useFfplay) {
		m_cachedFfplayPath = resolveFfplayPath();
		if (m_cachedFfplayPath.isEmpty()) {
			emit mediaLoadFailed(tr("未找到 ffplay.exe，请先在 deps 中安装/解压 FFmpeg"));
			emit mediaStatusMessage(tr("FFmpeg 未就绪"));
			return false;
		}
		emit mediaLoaded(m_currentFilePath);
		return true;
	}

	m_player->setMedia(QUrl::fromLocalFile(m_currentFilePath));

	emit mediaLoaded(m_currentFilePath);
	return true;
}

void MediaController::play()
{
	if (m_useFfplay) {
		if (!startFfplay(m_currentFilePath)) {
			emit mediaLoadFailed(tr("无法启动 ffplay"));
			emit mediaStatusMessage(tr("播放失败"));
			return;
		}
		emit mediaStatusMessage(tr("已使用 FFmpeg 播放"));
		return;
	}

	if (m_player) {
		m_player->play();
		emit mediaStatusMessage(tr("开始播放"));
	}
}

void MediaController::pause()
{
	if (m_useFfplay) {
		emit mediaStatusMessage(tr("FFmpeg 播放不支持应用内暂停"));
		return;
	}
	if (m_player) {
		m_player->pause();
		emit mediaStatusMessage(tr("已暂停"));
	}
}

void MediaController::stop()
{
	if (m_useFfplay) {
		if (m_ffplayProcess && m_ffplayProcess->state() != QProcess::NotRunning) {
			m_ffplayProcess->terminate();
		}
		emit mediaStatusMessage(tr("已停止"));
		return;
	}

	if (m_player) {
		m_player->stop();
		emit mediaStatusMessage(tr("已停止"));
	}
}

QString MediaController::currentFilePath() const
{
	return m_currentFilePath;
}

bool MediaController::isVideoFile(const QString &filePath) const
{
	const QFileInfo info(filePath);
	if (!info.exists() || !info.isFile()) {
		return false;
	}

	static const QStringList supportedExtensions = {
		QStringLiteral("mp4"),
		QStringLiteral("mkv"),
		QStringLiteral("avi"),
		QStringLiteral("mov"),
		QStringLiteral("wmv"),
		QStringLiteral("flv"),
		QStringLiteral("webm"),
		QStringLiteral("m4v")
	};

	return supportedExtensions.contains(info.suffix().toLower());

}

QString MediaController::resolveFfplayPath() const
{
	if (!m_cachedFfplayPath.isEmpty() && QFileInfo::exists(m_cachedFfplayPath)) {
		return m_cachedFfplayPath;
	}

	const QString baseDir = QDir::currentPath() + "/deps";
	const QStringList candidates = {
		baseDir + "/ffplay.exe",
		baseDir + "/ffmpeg/ffplay.exe",
		baseDir + "/ffmpeg/bin/ffplay.exe",
		baseDir + "/ffmpeg-master-latest-win64-gpl/bin/ffplay.exe"
	};

	for (const QString &candidate : candidates) {
		if (QFileInfo::exists(candidate)) {
			return candidate;
		}
	}

	QDirIterator it(baseDir, QStringList() << "ffplay.exe", QDir::Files, QDirIterator::Subdirectories);
	if (it.hasNext()) {
		return it.next();
	}

	return QString();
}

bool MediaController::startFfplay(const QString &filePath)
{
	if (!m_ffplayProcess || filePath.isEmpty()) {
		return false;
	}

	if (m_ffplayProcess->state() != QProcess::NotRunning) {
		m_ffplayProcess->terminate();
		m_ffplayProcess->waitForFinished(2000);
	}

	const QString ffplayPath = m_cachedFfplayPath.isEmpty() ? resolveFfplayPath() : m_cachedFfplayPath;
	if (ffplayPath.isEmpty()) {
		return false;
	}

	QStringList args;
	args << "-autoexit" << "-hide_banner" << "-loglevel" << "warning";
	args << "-window_title" << "qSrtTool Preview";
	args << filePath;

	m_ffplayProcess->setProgram(ffplayPath);
	m_ffplayProcess->setArguments(args);
	m_ffplayProcess->setWorkingDirectory(QFileInfo(ffplayPath).absolutePath());
	m_ffplayProcess->start();

	return m_ffplayProcess->waitForStarted(3000);
}
