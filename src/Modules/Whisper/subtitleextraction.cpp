#include "subtitleextraction.h"
#include "ui_subtitleextraction.h"

SubtitleExtraction::SubtitleExtraction(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::SubtitleExtraction)
{
    ui->setupUi(this);
}

SubtitleExtraction::~SubtitleExtraction()
{
    delete ui;
}
