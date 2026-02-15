#include "subtitleburning.h"
#include "ui_subtitleburning.h"

SubtitleBurning::SubtitleBurning(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::SubtitleBurning)
{
    ui->setupUi(this);
}

SubtitleBurning::~SubtitleBurning()
{
    delete ui;
}
