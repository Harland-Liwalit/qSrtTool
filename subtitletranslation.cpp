#include "subtitletranslation.h"
#include "ui_subtitletranslation.h"

SubtitleTranslation::SubtitleTranslation(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::SubtitleTranslation)
{
    ui->setupUi(this);
}

SubtitleTranslation::~SubtitleTranslation()
{
    delete ui;
}
