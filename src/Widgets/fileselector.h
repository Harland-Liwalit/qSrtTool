#ifndef FILESELECTOR_H
#define FILESELECTOR_H

#include <QWidget>

class QLineEdit;
class QPushButton;

class FileSelector : public QWidget
{
    Q_OBJECT

public:
    explicit FileSelector(QWidget *parent = nullptr);

    QString filePath() const;
    void setFilePath(const QString &path);

signals:
    void fileSelected(const QString &path);

private slots:
    void chooseFile();

private:
    QLineEdit *m_pathEdit = nullptr;
    QPushButton *m_browseButton = nullptr;
};

#endif // FILESELECTOR_H
