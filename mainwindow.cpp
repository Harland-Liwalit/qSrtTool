#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QButtonGroup>
#include <QToolButton>

#include "outputmanagement.h"
#include "subtitleburning.h"
#include "subtitleextraction.h"
#include "subtitletranslation.h"
#include "videodownloader.h"
#include "videoloader.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    QWidget *placeholder = ui->mainStackedWidget->findChild<QWidget *>("pagePlaceholder");
    if (placeholder) {
        ui->mainStackedWidget->removeWidget(placeholder);
        placeholder->deleteLater();
    }

    VideoDownloader *downloadPage = new VideoDownloader(this);
    VideoLoader *loaderPage = new VideoLoader(this);
    SubtitleExtraction *whisperPage = new SubtitleExtraction(this);
    SubtitleTranslation *translatePage = new SubtitleTranslation(this);
    SubtitleBurning *burnPage = new SubtitleBurning(this);
    OutputManagement *outputPage = new OutputManagement(this);

    ui->mainStackedWidget->addWidget(downloadPage);
    ui->mainStackedWidget->addWidget(loaderPage);
    ui->mainStackedWidget->addWidget(whisperPage);
    ui->mainStackedWidget->addWidget(translatePage);
    ui->mainStackedWidget->addWidget(burnPage);
    ui->mainStackedWidget->addWidget(outputPage);

    QButtonGroup *navGroup = new QButtonGroup(this);
    navGroup->setExclusive(true);
    navGroup->addButton(ui->navDownloadButton);
    navGroup->addButton(ui->navPreviewButton);
    navGroup->addButton(ui->navWhisperButton);
    navGroup->addButton(ui->navTranslateButton);
    navGroup->addButton(ui->navBurnButton);
    navGroup->addButton(ui->navOutputButton);

    auto bindNav = [this](QToolButton *button, QWidget *page) {
        connect(button, &QToolButton::clicked, this, [this, page]() {
            ui->mainStackedWidget->setCurrentWidget(page);
        });
    };

    bindNav(ui->navDownloadButton, downloadPage);
    bindNav(ui->navPreviewButton, loaderPage);
    bindNav(ui->navWhisperButton, whisperPage);
    bindNav(ui->navTranslateButton, translatePage);
    bindNav(ui->navBurnButton, burnPage);
    bindNav(ui->navOutputButton, outputPage);

    ui->navPreviewButton->setChecked(true);
    ui->mainStackedWidget->setCurrentWidget(loaderPage);
}

MainWindow::~MainWindow()
{
    delete ui;
}

