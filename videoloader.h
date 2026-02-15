#ifndef VIDEOLOADER_H
#define VIDEOLOADER_H

#include <QWidget>

namespace Ui {
class VideoLoader;
}

class VideoLoader : public QWidget
{
    Q_OBJECT

public:
    explicit VideoLoader(QWidget *parent = nullptr);
    ~VideoLoader();

private:
    Ui::VideoLoader *ui;
};

#endif // VIDEOLOADER_H
