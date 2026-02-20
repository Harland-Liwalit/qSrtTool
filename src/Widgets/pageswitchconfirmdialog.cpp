#include "pageswitchconfirmdialog.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

PageSwitchConfirmDialog::PageSwitchConfirmDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("任务进行中"));
    setModal(true);
    setMinimumWidth(420);

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(16, 14, 16, 14);
    mainLayout->setSpacing(12);

    m_messageLabel = new QLabel(this);
    m_messageLabel->setWordWrap(true);
    m_messageLabel->setText(tr("当前有任务正在执行，是否转到新功能？"));

    m_skipPromptCheck = new QCheckBox(tr("不再提示"), this);

    auto *buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();

    m_cancelButton = new QPushButton(tr("留在当前页面"), this);
    m_confirmButton = new QPushButton(tr("转到新功能"), this);
    m_confirmButton->setDefault(true);

    buttonLayout->addWidget(m_cancelButton);
    buttonLayout->addWidget(m_confirmButton);

    mainLayout->addWidget(m_messageLabel);
    mainLayout->addWidget(m_skipPromptCheck);
    mainLayout->addLayout(buttonLayout);

    connect(m_confirmButton, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_cancelButton, &QPushButton::clicked, this, &QDialog::reject);
}

void PageSwitchConfirmDialog::setTargetName(const QString &targetName)
{
    m_messageLabel->setText(tr("当前有任务正在执行，是否转到“%1”功能？\n切换后将停止当前页面正在执行的任务。").arg(targetName));
}

bool PageSwitchConfirmDialog::skipPromptForCurrentTask() const
{
    return m_skipPromptCheck && m_skipPromptCheck->isChecked();
}
