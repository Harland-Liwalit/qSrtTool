#include "promptediting.h"
#include "ui_promptediting.h"

#include <QAbstractItemModel>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLayout>
#include <QList>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QTableWidgetItem>

namespace {
constexpr int kFixedCharacterId = 100001;

QString defaultPromptIdentifier(int index)
{
    return QString("prompt_%1").arg(index + 1);
}

QString promptDisplayName(const QJsonArray &prompts, const QString &identifier)
{
    const QString trimmedIdentifier = identifier.trimmed();
    if (trimmedIdentifier.isEmpty()) {
        return QString();
    }

    for (const QJsonValue &value : prompts) {
        const QJsonObject prompt = value.toObject();
        if (prompt.value("identifier").toString().trimmed() == trimmedIdentifier) {
            const QString name = prompt.value("name").toString().trimmed();
            return name.isEmpty() ? trimmedIdentifier : name;
        }
    }

    return trimmedIdentifier;
}
}

PromptEditing::PromptEditing(const QString &presetDirectory,
                             const QString &presetFilePath,
                             QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::PromptEditing)
    , m_presetDirectory(presetDirectory)
    , m_currentFilePath(presetFilePath)
{
    ui->setupUi(this);
    setModal(true);
    setWindowTitle(tr("qSrtTool 预设编辑器"));

    setupUiBehavior();
    setupConnections();

    if (!m_currentFilePath.isEmpty() && QFileInfo::exists(m_currentFilePath)) {
        loadPresetFromFile(m_currentFilePath);
    } else {
        applyPresetToUi(createDefaultPreset());
    }

    refreshJsonPreview();
}

PromptEditing::~PromptEditing()
{
    delete ui;
}

QString PromptEditing::savedPresetPath() const
{
    return m_savedPresetPath;
}

void PromptEditing::setupUiBehavior()
{
    ui->chatSourceComboBox->setEditable(true);

    if (ui->topBarFrame) {
        ui->topBarFrame->setMinimumHeight(64);
        ui->topBarFrame->setMaximumHeight(84);
        QSizePolicy topPolicy = ui->topBarFrame->sizePolicy();
        topPolicy.setVerticalPolicy(QSizePolicy::Fixed);
        ui->topBarFrame->setSizePolicy(topPolicy);
    }

    if (ui->mainLayout) {
        ui->mainLayout->setStretch(0, 0);
        ui->mainLayout->setStretch(1, 1);
        ui->mainLayout->setStretch(2, 0);
    }

    if (ui->promptOrderTableWidget) {
        ui->promptOrderTableWidget->horizontalHeader()->setStretchLastSection(false);
        ui->promptOrderTableWidget->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        ui->promptOrderTableWidget->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
        ui->promptOrderTableWidget->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
        ui->promptOrderTableWidget->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
        ui->promptOrderTableWidget->setAlternatingRowColors(true);
        ui->promptOrderTableWidget->verticalHeader()->setVisible(false);
        ui->promptOrderTableWidget->setSelectionBehavior(QAbstractItemView::SelectRows);
        ui->promptOrderTableWidget->setSelectionMode(QAbstractItemView::SingleSelection);
    }

    if (ui->editorSplitter) {
        ui->editorSplitter->setStretchFactor(0, 0);
        ui->editorSplitter->setStretchFactor(1, 1);
    }

    if (ui->arrangementSplitter) {
        ui->arrangementSplitter->setStretchFactor(0, 3);
        ui->arrangementSplitter->setStretchFactor(1, 2);
        ui->arrangementSplitter->setSizes(QList<int>({680, 420}));
    }

    ui->manualOrderSpinBox->setValue(1);
    ui->characterIdSpinBox->setValue(kFixedCharacterId);
    ui->characterIdSpinBox->setEnabled(false);
}

void PromptEditing::setupConnections()
{
    connect(ui->cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    connect(ui->savePresetButton, &QPushButton::clicked, this, &PromptEditing::savePresetAndAccept);

    connect(ui->newPresetButton, &QPushButton::clicked, this, &PromptEditing::createNewPreset);
    connect(ui->importPresetFileButton, &QPushButton::clicked, this, &PromptEditing::importPresetJson);
    connect(ui->exportPresetButton, &QPushButton::clicked, this, &PromptEditing::exportPresetJson);

    connect(ui->addPromptButton, &QPushButton::clicked, this, &PromptEditing::addPrompt);
    connect(ui->removePromptButton, &QPushButton::clicked, this, &PromptEditing::removePrompt);
    connect(ui->promptListWidget, &QListWidget::currentRowChanged, this, [this](int row) {
        if (m_updatingUi) {
            return;
        }
        commitPromptEditor();
        loadPromptToEditor(row);
        refreshJsonPreview();
    });

    auto promptFieldChanged = [this]() {
        if (m_updatingUi) {
            return;
        }
        commitPromptEditor();
        refreshJsonPreview();
    };
    connect(ui->promptNameLineEdit, &QLineEdit::textChanged, this, promptFieldChanged);
    connect(ui->promptIdentifierLineEdit, &QLineEdit::textChanged, this, promptFieldChanged);
    connect(ui->promptRoleComboBox, &QComboBox::currentTextChanged, this, promptFieldChanged);
    connect(ui->promptContentTextEdit, &QPlainTextEdit::textChanged, this, promptFieldChanged);

    connect(ui->addOrderItemButton, &QPushButton::clicked, this, &PromptEditing::addOrderItem);
    connect(ui->removeOrderItemButton, &QPushButton::clicked, this, &PromptEditing::removeOrderItem);
    connect(ui->applyOrderButton, &QPushButton::clicked, this, &PromptEditing::applyManualOrder);

    connect(ui->promptOrderTableWidget, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *) {
        if (m_updatingUi) {
            return;
        }
        renumberOrderTable();
        refreshJsonPreview();
    });

    connect(ui->promptOrderTableWidget->model(),
            &QAbstractItemModel::rowsMoved,
            this,
            [this](const QModelIndex &, int, int, const QModelIndex &, int) {
                if (m_updatingUi) {
                    return;
                }
                renumberOrderTable();
                refreshJsonPreview();
            });

    auto scalarChanged = [this]() {
        if (!m_updatingUi) {
            refreshJsonPreview();
        }
    };

    connect(ui->presetNameLineEdit, &QLineEdit::textChanged, this, scalarChanged);
    connect(ui->chatSourceComboBox, &QComboBox::currentTextChanged, this, scalarChanged);
    connect(ui->seedSpinBox, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), this, scalarChanged);
    connect(ui->candidateSpinBox, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), this, scalarChanged);
    connect(ui->openRouterModelLineEdit, &QLineEdit::textChanged, this, scalarChanged);
    connect(ui->claudeModelLineEdit, &QLineEdit::textChanged, this, scalarChanged);
    connect(ui->googleModelLineEdit, &QLineEdit::textChanged, this, scalarChanged);
    connect(ui->customModelLineEdit, &QLineEdit::textChanged, this, scalarChanged);

    connect(ui->temperatureDoubleSpinBox,
            static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged),
            this,
            scalarChanged);
    connect(ui->topPDoubleSpinBox,
            static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged),
            this,
            scalarChanged);
    connect(ui->topKSpinBox, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), this, scalarChanged);
    connect(ui->topADoubleSpinBox,
            static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged),
            this,
            scalarChanged);
    connect(ui->minPDoubleSpinBox,
            static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged),
            this,
            scalarChanged);
    connect(ui->frequencyPenaltyDoubleSpinBox,
            static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged),
            this,
            scalarChanged);
    connect(ui->presencePenaltyDoubleSpinBox,
            static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged),
            this,
            scalarChanged);
    connect(ui->repetitionPenaltyDoubleSpinBox,
            static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged),
            this,
            scalarChanged);

    connect(ui->streamOpenAiCheckBox, &QCheckBox::toggled, this, scalarChanged);
    connect(ui->showThoughtsCheckBox, &QCheckBox::toggled, this, scalarChanged);
    connect(ui->enableWebSearchCheckBox, &QCheckBox::toggled, this, scalarChanged);
    connect(ui->functionCallingCheckBox, &QCheckBox::toggled, this, scalarChanged);
}

QJsonObject PromptEditing::createDefaultPreset() const
{
    QJsonObject mainPrompt;
    mainPrompt.insert("name", "Main Prompt");
    mainPrompt.insert("identifier", "main");
    mainPrompt.insert("role", "system");
    mainPrompt.insert("content", "");
    mainPrompt.insert("enabled", true);

    QJsonArray prompts;
    prompts.append(mainPrompt);

    QJsonObject orderEntry;
    orderEntry.insert("identifier", "main");
    orderEntry.insert("enabled", true);

    QJsonArray orderArray;
    orderArray.append(orderEntry);

    QJsonObject promptOrderItem;
    promptOrderItem.insert("character_id", kFixedCharacterId);
    promptOrderItem.insert("order", orderArray);

    QJsonArray promptOrder;
    promptOrder.append(promptOrderItem);

    QJsonObject preset;
    preset.insert("name", "");
    preset.insert("chat_completion_source", "openrouter");
    preset.insert("openrouter_model", "");
    preset.insert("claude_model", "");
    preset.insert("google_model", "");
    preset.insert("custom_model", "");
    preset.insert("temperature", 1.0);
    preset.insert("top_p", 1.0);
    preset.insert("top_k", 0);
    preset.insert("top_a", 1.0);
    preset.insert("min_p", 0.0);
    preset.insert("frequency_penalty", 0.0);
    preset.insert("presence_penalty", 0.0);
    preset.insert("repetition_penalty", 1.0);
    preset.insert("stream_openai", true);
    preset.insert("show_thoughts", false);
    preset.insert("enable_web_search", false);
    preset.insert("function_calling", false);
    preset.insert("request_images", false);
    preset.insert("image_inlining", false);
    preset.insert("seed", -1);
    preset.insert("n", 1);
    preset.insert("prompts", prompts);
    preset.insert("prompt_order", promptOrder);

    return preset;
}

void PromptEditing::loadPresetFromFile(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("读取失败"), tr("无法打开预设文件：%1").arg(filePath));
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        QMessageBox::warning(this,
                             tr("格式错误"),
                             tr("预设不是有效 JSON 对象：%1").arg(parseError.errorString()));
        return;
    }

    QJsonObject presetObject = document.object();
    const QString fallbackName = QFileInfo(filePath).completeBaseName().trimmed();
    if (presetObject.value("name").toString().trimmed().isEmpty() && !fallbackName.isEmpty()) {
        presetObject.insert("name", fallbackName);
    }

    m_currentFilePath = filePath;
    applyPresetToUi(presetObject);
}

void PromptEditing::applyPresetToUi(const QJsonObject &presetObject)
{
    m_updatingUi = true;
    m_basePresetObject = presetObject;

    ui->presetNameLineEdit->setText(presetObject.value("name").toString());
    ui->chatSourceComboBox->setCurrentText(presetObject.value("chat_completion_source").toString("openrouter"));

    ui->seedSpinBox->setValue(presetObject.value("seed").toInt(-1));
    ui->candidateSpinBox->setValue(qMax(1, presetObject.value("n").toInt(1)));

    ui->openRouterModelLineEdit->setText(presetObject.value("openrouter_model").toString());
    ui->claudeModelLineEdit->setText(presetObject.value("claude_model").toString());
    ui->googleModelLineEdit->setText(presetObject.value("google_model").toString());
    ui->customModelLineEdit->setText(presetObject.value("custom_model").toString());

    ui->temperatureDoubleSpinBox->setValue(presetObject.value("temperature").toDouble(1.0));
    ui->topPDoubleSpinBox->setValue(presetObject.value("top_p").toDouble(1.0));
    ui->topKSpinBox->setValue(presetObject.value("top_k").toInt(0));
    ui->topADoubleSpinBox->setValue(presetObject.value("top_a").toDouble(1.0));
    ui->minPDoubleSpinBox->setValue(presetObject.value("min_p").toDouble(0.0));
    ui->frequencyPenaltyDoubleSpinBox->setValue(presetObject.value("frequency_penalty").toDouble(0.0));
    ui->presencePenaltyDoubleSpinBox->setValue(presetObject.value("presence_penalty").toDouble(0.0));
    ui->repetitionPenaltyDoubleSpinBox->setValue(presetObject.value("repetition_penalty").toDouble(1.0));

    ui->streamOpenAiCheckBox->setChecked(presetObject.value("stream_openai").toBool(true));
    ui->showThoughtsCheckBox->setChecked(presetObject.value("show_thoughts").toBool(false));
    ui->enableWebSearchCheckBox->setChecked(presetObject.value("enable_web_search").toBool(false));
    ui->functionCallingCheckBox->setChecked(presetObject.value("function_calling").toBool(false));

    m_prompts = presetObject.value("prompts").toArray();
    if (m_prompts.isEmpty()) {
        QJsonObject prompt;
        prompt.insert("name", "Main Prompt");
        prompt.insert("identifier", "main");
        prompt.insert("role", "system");
        prompt.insert("content", "");
        prompt.insert("enabled", true);
        m_prompts.append(prompt);
    }

    refreshPromptList();
    ui->promptListWidget->setCurrentRow(0);
    loadPromptToEditor(0);

    m_promptOrderByCharacter.clear();
    QJsonArray fixedOrderArray;
    const QJsonArray promptOrderArray = presetObject.value("prompt_order").toArray();
    for (const QJsonValue &value : promptOrderArray) {
        const QJsonObject rowObject = value.toObject();
        const int characterId = rowObject.value("character_id").toInt(kFixedCharacterId);
        if (characterId == kFixedCharacterId) {
            fixedOrderArray = rowObject.value("order").toArray();
            break;
        }
        if (fixedOrderArray.isEmpty()) {
            fixedOrderArray = rowObject.value("order").toArray();
        }
    }

    m_currentCharacterId = kFixedCharacterId;
    m_promptOrderByCharacter.insert(kFixedCharacterId, fixedOrderArray);
    ui->characterIdSpinBox->setValue(kFixedCharacterId);
    loadOrderTableForCharacter(kFixedCharacterId);

    m_updatingUi = false;
}

void PromptEditing::refreshPromptList()
{
    const QSignalBlocker blocker(ui->promptListWidget);
    ui->promptListWidget->clear();
    for (int i = 0; i < m_prompts.size(); ++i) {
        const QJsonObject prompt = m_prompts.at(i).toObject();
        QString label = prompt.value("name").toString().trimmed();
        if (label.isEmpty()) {
            label = prompt.value("identifier").toString().trimmed();
        }
        if (label.isEmpty()) {
            label = defaultPromptIdentifier(i);
        }
        ui->promptListWidget->addItem(label);
    }
}

void PromptEditing::loadPromptToEditor(int row)
{
    m_updatingUi = true;
    m_currentPromptRow = row;

    if (row < 0 || row >= m_prompts.size()) {
        ui->promptNameLineEdit->clear();
        ui->promptIdentifierLineEdit->clear();
        ui->promptRoleComboBox->setCurrentText("system");
        ui->promptContentTextEdit->clear();
        m_updatingUi = false;
        return;
    }

    const QJsonObject prompt = m_prompts.at(row).toObject();
    ui->promptNameLineEdit->setText(prompt.value("name").toString());
    ui->promptIdentifierLineEdit->setText(prompt.value("identifier").toString(defaultPromptIdentifier(row)));
    ui->promptRoleComboBox->setCurrentText(prompt.value("role").toString("system"));
    ui->promptContentTextEdit->setPlainText(prompt.value("content").toString());

    m_updatingUi = false;
}

void PromptEditing::commitPromptEditor()
{
    if (m_updatingUi || m_currentPromptRow < 0 || m_currentPromptRow >= m_prompts.size()) {
        return;
    }

    QJsonObject prompt = m_prompts.at(m_currentPromptRow).toObject();
    prompt.insert("name", ui->promptNameLineEdit->text());
    prompt.insert("identifier", ui->promptIdentifierLineEdit->text());
    prompt.insert("role", ui->promptRoleComboBox->currentText());
    prompt.insert("content", ui->promptContentTextEdit->toPlainText());
    if (!prompt.contains("enabled")) {
        prompt.insert("enabled", true);
    }

    m_prompts.replace(m_currentPromptRow, prompt);

    if (QListWidgetItem *item = ui->promptListWidget->item(m_currentPromptRow)) {
        QString label = prompt.value("name").toString().trimmed();
        if (label.isEmpty()) {
            label = prompt.value("identifier").toString().trimmed();
        }
        if (label.isEmpty()) {
            label = defaultPromptIdentifier(m_currentPromptRow);
        }
        item->setText(label);
    }

    renumberOrderTable();
}

void PromptEditing::addPrompt()
{
    commitPromptEditor();

    QJsonObject prompt;
    const int nextIndex = m_prompts.size();
    const QString identifier = defaultPromptIdentifier(nextIndex);
    prompt.insert("name", QString("Prompt %1").arg(nextIndex + 1));
    prompt.insert("identifier", identifier);
    prompt.insert("role", "system");
    prompt.insert("content", "");
    prompt.insert("enabled", true);
    m_prompts.append(prompt);

    refreshPromptList();
    const int newRow = m_prompts.size() - 1;
    ui->promptListWidget->setCurrentRow(newRow);
    loadPromptToEditor(newRow);
    renumberOrderTable();
    refreshJsonPreview();
}

void PromptEditing::removePrompt()
{
    const int row = ui->promptListWidget->currentRow();
    if (row < 0 || row >= m_prompts.size()) {
        return;
    }

    m_prompts.removeAt(row);
    if (m_prompts.isEmpty()) {
        QJsonObject prompt;
        prompt.insert("name", "Main Prompt");
        prompt.insert("identifier", "main");
        prompt.insert("role", "system");
        prompt.insert("content", "");
        prompt.insert("enabled", true);
        m_prompts.append(prompt);
    }

    refreshPromptList();
    const int targetRow = qMin(row, m_prompts.size() - 1);
    ui->promptListWidget->setCurrentRow(targetRow);
    loadPromptToEditor(targetRow);
    renumberOrderTable();
    refreshJsonPreview();
}

void PromptEditing::loadOrderTableForCharacter(int characterId)
{
    m_updatingUi = true;
    applyOrderJsonArrayToTable(m_promptOrderByCharacter.value(characterId));
    m_updatingUi = false;
    renumberOrderTable();
}

void PromptEditing::saveOrderTableForCurrentCharacter()
{
    m_currentCharacterId = kFixedCharacterId;
    m_promptOrderByCharacter.insert(kFixedCharacterId, orderTableToJsonArray());
}

QJsonArray PromptEditing::orderTableToJsonArray() const
{
    QJsonArray orderArray;
    for (int row = 0; row < ui->promptOrderTableWidget->rowCount(); ++row) {
        const QTableWidgetItem *identifierItem = ui->promptOrderTableWidget->item(row, 2);
        const QTableWidgetItem *enabledItem = ui->promptOrderTableWidget->item(row, 3);

        const QString identifier = identifierItem ? identifierItem->text().trimmed() : QString();
        if (identifier.isEmpty()) {
            continue;
        }

        QJsonObject orderEntry;
        orderEntry.insert("identifier", identifier);
        orderEntry.insert("enabled", enabledItem && enabledItem->checkState() == Qt::Checked);
        orderArray.append(orderEntry);
    }
    return orderArray;
}

void PromptEditing::applyOrderJsonArrayToTable(const QJsonArray &orderArray)
{
    ui->promptOrderTableWidget->setRowCount(0);

    QJsonArray normalizedArray = orderArray;
    if (normalizedArray.isEmpty()) {
        for (const QJsonValue &value : m_prompts) {
            const QJsonObject prompt = value.toObject();
            const QString identifier = prompt.value("identifier").toString().trimmed();
            if (identifier.isEmpty()) {
                continue;
            }

            QJsonObject entry;
            entry.insert("identifier", identifier);
            entry.insert("enabled", true);
            normalizedArray.append(entry);
        }
    }

    for (int row = 0; row < normalizedArray.size(); ++row) {
        const QJsonObject entry = normalizedArray.at(row).toObject();
        const QString identifier = entry.value("identifier").toString();
        const QString name = promptDisplayName(m_prompts, identifier);
        ui->promptOrderTableWidget->insertRow(row);

        QTableWidgetItem *orderItem = new QTableWidgetItem(QString::number(row + 1));
        orderItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);

        QTableWidgetItem *nameItem = new QTableWidgetItem(name);
        nameItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);

        QTableWidgetItem *identifierItem = new QTableWidgetItem(identifier);
        identifierItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable);

        QTableWidgetItem *enabledItem = new QTableWidgetItem();
        enabledItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
        enabledItem->setCheckState(entry.value("enabled").toBool(true) ? Qt::Checked : Qt::Unchecked);

        ui->promptOrderTableWidget->setItem(row, 0, orderItem);
        ui->promptOrderTableWidget->setItem(row, 1, nameItem);
        ui->promptOrderTableWidget->setItem(row, 2, identifierItem);
        ui->promptOrderTableWidget->setItem(row, 3, enabledItem);
    }

    if (ui->promptOrderTableWidget->rowCount() > 0) {
        ui->promptOrderTableWidget->selectRow(0);
    }
}

void PromptEditing::renumberOrderTable()
{
    m_updatingUi = true;
    for (int row = 0; row < ui->promptOrderTableWidget->rowCount(); ++row) {
        QTableWidgetItem *orderItem = ui->promptOrderTableWidget->item(row, 0);
        if (!orderItem) {
            orderItem = new QTableWidgetItem();
            orderItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            ui->promptOrderTableWidget->setItem(row, 0, orderItem);
        }
        orderItem->setText(QString::number(row + 1));

        QTableWidgetItem *identifierItem = ui->promptOrderTableWidget->item(row, 2);
        QTableWidgetItem *nameItem = ui->promptOrderTableWidget->item(row, 1);
        if (!nameItem) {
            nameItem = new QTableWidgetItem();
            nameItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            ui->promptOrderTableWidget->setItem(row, 1, nameItem);
        }
        const QString identifier = identifierItem ? identifierItem->text().trimmed() : QString();
        nameItem->setText(promptDisplayName(m_prompts, identifier));
    }
    m_updatingUi = false;
}

void PromptEditing::addOrderItem()
{
    const int row = ui->promptOrderTableWidget->rowCount();
    ui->promptOrderTableWidget->insertRow(row);

    QString identifier = QString("prompt_%1").arg(row + 1);
    const int promptRow = ui->promptListWidget->currentRow();
    if (promptRow >= 0 && promptRow < m_prompts.size()) {
        const QString currentIdentifier = m_prompts.at(promptRow).toObject().value("identifier").toString();
        if (!currentIdentifier.trimmed().isEmpty()) {
            identifier = currentIdentifier;
        }
    }

    QTableWidgetItem *orderItem = new QTableWidgetItem(QString::number(row + 1));
    orderItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);

    QTableWidgetItem *nameItem = new QTableWidgetItem(promptDisplayName(m_prompts, identifier));
    nameItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);

    QTableWidgetItem *identifierItem = new QTableWidgetItem(identifier);
    identifierItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable);

    QTableWidgetItem *enabledItem = new QTableWidgetItem();
    enabledItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
    enabledItem->setCheckState(Qt::Checked);

    ui->promptOrderTableWidget->setItem(row, 0, orderItem);
    ui->promptOrderTableWidget->setItem(row, 1, nameItem);
    ui->promptOrderTableWidget->setItem(row, 2, identifierItem);
    ui->promptOrderTableWidget->setItem(row, 3, enabledItem);
    ui->promptOrderTableWidget->selectRow(row);

    renumberOrderTable();
    refreshJsonPreview();
}

void PromptEditing::removeOrderItem()
{
    const int row = ui->promptOrderTableWidget->currentRow();
    if (row < 0) {
        return;
    }

    ui->promptOrderTableWidget->removeRow(row);
    const int remainingRows = ui->promptOrderTableWidget->rowCount();
    if (remainingRows > 0) {
        ui->promptOrderTableWidget->selectRow(qMin(row, remainingRows - 1));
    }

    renumberOrderTable();
    refreshJsonPreview();
}

void PromptEditing::applyManualOrder()
{
    const int currentRow = ui->promptOrderTableWidget->currentRow();
    if (currentRow < 0) {
        return;
    }

    const int rowCount = ui->promptOrderTableWidget->rowCount();
    if (rowCount <= 1) {
        return;
    }

    int targetRow = ui->manualOrderSpinBox->value() - 1;
    targetRow = qBound(0, targetRow, rowCount - 1);
    if (targetRow == currentRow) {
        return;
    }

    QTableWidgetItem *orderItem = ui->promptOrderTableWidget->takeItem(currentRow, 0);
    QTableWidgetItem *nameItem = ui->promptOrderTableWidget->takeItem(currentRow, 1);
    QTableWidgetItem *identifierItem = ui->promptOrderTableWidget->takeItem(currentRow, 2);
    QTableWidgetItem *enabledItem = ui->promptOrderTableWidget->takeItem(currentRow, 3);

    ui->promptOrderTableWidget->removeRow(currentRow);
    if (targetRow > currentRow) {
        targetRow -= 1;
    }

    ui->promptOrderTableWidget->insertRow(targetRow);
    ui->promptOrderTableWidget->setItem(targetRow, 0, orderItem);
    ui->promptOrderTableWidget->setItem(targetRow, 1, nameItem);
    ui->promptOrderTableWidget->setItem(targetRow, 2, identifierItem);
    ui->promptOrderTableWidget->setItem(targetRow, 3, enabledItem);
    ui->promptOrderTableWidget->selectRow(targetRow);

    renumberOrderTable();
    refreshJsonPreview();
}

QJsonObject PromptEditing::buildPresetFromUi()
{
    commitPromptEditor();
    saveOrderTableForCurrentCharacter();

    QJsonObject presetObject = m_basePresetObject;
    presetObject.insert("name", ui->presetNameLineEdit->text().trimmed());
    presetObject.insert("chat_completion_source", ui->chatSourceComboBox->currentText().trimmed());

    presetObject.insert("openrouter_model", ui->openRouterModelLineEdit->text().trimmed());
    presetObject.insert("claude_model", ui->claudeModelLineEdit->text().trimmed());
    presetObject.insert("google_model", ui->googleModelLineEdit->text().trimmed());
    presetObject.insert("custom_model", ui->customModelLineEdit->text().trimmed());

    presetObject.insert("temperature", ui->temperatureDoubleSpinBox->value());
    presetObject.insert("top_p", ui->topPDoubleSpinBox->value());
    presetObject.insert("top_k", ui->topKSpinBox->value());
    presetObject.insert("top_a", ui->topADoubleSpinBox->value());
    presetObject.insert("min_p", ui->minPDoubleSpinBox->value());
    presetObject.insert("frequency_penalty", ui->frequencyPenaltyDoubleSpinBox->value());
    presetObject.insert("presence_penalty", ui->presencePenaltyDoubleSpinBox->value());
    presetObject.insert("repetition_penalty", ui->repetitionPenaltyDoubleSpinBox->value());

    presetObject.insert("stream_openai", ui->streamOpenAiCheckBox->isChecked());
    presetObject.insert("show_thoughts", ui->showThoughtsCheckBox->isChecked());
    presetObject.insert("enable_web_search", ui->enableWebSearchCheckBox->isChecked());
    presetObject.insert("function_calling", ui->functionCallingCheckBox->isChecked());

    presetObject.insert("request_images", false);
    presetObject.insert("image_inlining", false);
    presetObject.insert("seed", ui->seedSpinBox->value());
    presetObject.insert("n", ui->candidateSpinBox->value());
    presetObject.insert("prompts", m_prompts);

    QJsonArray promptOrderArray;
    QJsonObject orderObject;
    orderObject.insert("character_id", kFixedCharacterId);
    orderObject.insert("order", m_promptOrderByCharacter.value(kFixedCharacterId));
    promptOrderArray.append(orderObject);
    presetObject.insert("prompt_order", promptOrderArray);

    return presetObject;
}

void PromptEditing::refreshJsonPreview()
{
    const QJsonDocument document(buildPresetFromUi());
    ui->jsonPreviewTextEdit->setPlainText(QString::fromUtf8(document.toJson(QJsonDocument::Indented)));
}

void PromptEditing::createNewPreset()
{
    m_currentFilePath.clear();
    applyPresetToUi(createDefaultPreset());
    refreshJsonPreview();
}

void PromptEditing::importPresetJson()
{
    const QString filePath = QFileDialog::getOpenFileName(this,
                                                          tr("导入预设"),
                                                          QDir::homePath(),
                                                          tr("JSON 文件 (*.json)"));
    if (filePath.isEmpty()) {
        return;
    }

    loadPresetFromFile(filePath);

    const QFileInfo fileInfo(filePath);
    m_currentFilePath = QDir(m_presetDirectory).filePath(fileInfo.fileName());
    refreshJsonPreview();
}

void PromptEditing::exportPresetJson()
{
    QString suggestedPath = m_currentFilePath;
    if (suggestedPath.isEmpty()) {
        QString baseName = ui->presetNameLineEdit->text().trimmed();
        if (baseName.isEmpty()) {
            baseName = QString("preset_%1").arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"));
        }
        suggestedPath = QDir(m_presetDirectory).filePath(sanitizeFileName(baseName) + ".json");
    }

    const QString exportPath = QFileDialog::getSaveFileName(this,
                                                            tr("导出预设"),
                                                            suggestedPath,
                                                            tr("JSON 文件 (*.json)"));
    if (exportPath.isEmpty()) {
        return;
    }

    QFile file(exportPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        QMessageBox::warning(this, tr("导出失败"), tr("无法写入文件：%1").arg(exportPath));
        return;
    }

    const QJsonDocument document(buildPresetFromUi());
    file.write(document.toJson(QJsonDocument::Indented));
}

void PromptEditing::savePresetAndAccept()
{
    QDir presetDir(m_presetDirectory);
    if (!presetDir.exists() && !QDir().mkpath(m_presetDirectory)) {
        QMessageBox::warning(this, tr("保存失败"), tr("无法创建预设目录：%1").arg(m_presetDirectory));
        return;
    }

    QString targetPath = m_currentFilePath;
    if (targetPath.isEmpty()) {
        QString baseName = ui->presetNameLineEdit->text().trimmed();
        if (baseName.isEmpty()) {
            baseName = QString("preset_%1").arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"));
        }
        targetPath = presetDir.filePath(sanitizeFileName(baseName) + ".json");
    }

    QFile file(targetPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        QMessageBox::warning(this, tr("保存失败"), tr("无法写入文件：%1").arg(targetPath));
        return;
    }

    const QJsonDocument document(buildPresetFromUi());
    file.write(document.toJson(QJsonDocument::Indented));

    m_savedPresetPath = targetPath;
    m_currentFilePath = targetPath;
    accept();
}

QString PromptEditing::sanitizeFileName(const QString &fileName) const
{
    QString sanitized = fileName;
    sanitized.replace('\\', '_');
    sanitized.replace('/', '_');
    sanitized.replace(':', '_');
    sanitized.replace('*', '_');
    sanitized.replace('?', '_');
    sanitized.replace('"', '_');
    sanitized.replace('<', '_');
    sanitized.replace('>', '_');
    sanitized.replace('|', '_');
    sanitized = sanitized.trimmed();

    if (sanitized.isEmpty()) {
        sanitized = "preset";
    }

    return sanitized;
}
