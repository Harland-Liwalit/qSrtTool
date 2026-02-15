#include "videoloader.h"
#include "ui_videoloader.h"

VideoLoader::VideoLoader(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::VideoLoader)
{
    ui->setupUi(this);
}

VideoLoader::~VideoLoader()
{
    delete ui;
}
