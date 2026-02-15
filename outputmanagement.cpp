#include "outputmanagement.h"
#include "ui_outputmanagement.h"

OutputManagement::OutputManagement(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::OutputManagement)
{
    ui->setupUi(this);
}

OutputManagement::~OutputManagement()
{
    delete ui;
}
