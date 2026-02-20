#include "fileselector.h"

#include <QFileDialog>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>

FileSelector::FileSelector(QWidget *parent)
	: QWidget(parent)
{
	auto *layout = new QHBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(8);

	m_pathEdit = new QLineEdit(this);
	m_pathEdit->setPlaceholderText(tr("请选择视频文件"));
	m_pathEdit->setReadOnly(true);

	m_browseButton = new QPushButton(tr("选择文件"), this);

	layout->addWidget(m_pathEdit, 1);
	layout->addWidget(m_browseButton);

	connect(m_browseButton, &QPushButton::clicked, this, &FileSelector::chooseFile);
}

QString FileSelector::filePath() const
{
	return m_pathEdit ? m_pathEdit->text() : QString();
}

void FileSelector::setFilePath(const QString &path)
{
	if (!m_pathEdit) {
		return;
	}

	m_pathEdit->setText(path);
}

void FileSelector::chooseFile()
{
	const QString file = QFileDialog::getOpenFileName(
		this,
		tr("选择视频文件"),
		QString(),
		tr("视频文件 (*.mp4 *.mkv *.avi *.mov *.wmv *.flv *.webm *.m4v);;所有文件 (*.*)"));

	if (file.isEmpty()) {
		return;
	}

	setFilePath(file);
	emit fileSelected(file);

}
