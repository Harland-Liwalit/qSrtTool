#include "videodownloader.h"
#include "ui_videodownloader.h"

VideoDownloader::VideoDownloader(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::VideoDownloader)
{
    ui->setupUi(this);
}

VideoDownloader::~VideoDownloader()
{
    delete ui;
}
