#include "subtitletranslation.h"
#include "ui_subtitletranslation.h"

#include "llmserviceclient.h"
#include "promptediting.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QLineEdit>
#include <QJsonArray>
#include <QJsonObject>
#include <QMessageBox>
#include <QSettings>

#ifdef Q_OS_WIN
#include <windows.h>
#include <wincrypt.h>
#endif

namespace {
QString presetDirectoryPath()
{
    return QDir::currentPath() + "/presets/translator";
}

QString secretApiKeyStorageKey()
{
    return QStringLiteral("translator/security/api_key");
}

QString secretServerPasswordStorageKey()
{
    return QStringLiteral("translator/security/server_password");
}

QString encryptSecret(const QString &plainText)
{
    if (plainText.isEmpty()) {
        return QString();
    }

#ifdef Q_OS_WIN
    const QByteArray utf8 = plainText.toUtf8();
    DATA_BLOB inputBlob;
    inputBlob.pbData = reinterpret_cast<BYTE *>(const_cast<char *>(utf8.constData()));
    inputBlob.cbData = static_cast<DWORD>(utf8.size());

    DATA_BLOB outputBlob;
    outputBlob.pbData = nullptr;
    outputBlob.cbData = 0;

    if (!CryptProtectData(&inputBlob,
                          L"qSrtTool Translator Secret",
                          nullptr,
                          nullptr,
                          nullptr,
                          CRYPTPROTECT_UI_FORBIDDEN,
                          &outputBlob)) {
        return QString();
    }

    QByteArray cipher(reinterpret_cast<const char *>(outputBlob.pbData), outputBlob.cbData);
    LocalFree(outputBlob.pbData);
    return QString::fromLatin1(cipher.toBase64());
#else
    return QString::fromLatin1(qCompress(plainText.toUtf8(), 9).toBase64());
#endif
}

QString decryptSecret(const QString &cipherText)
{
    if (cipherText.isEmpty()) {
        return QString();
    }

#ifdef Q_OS_WIN
    const QByteArray cipher = QByteArray::fromBase64(cipherText.toLatin1());
    if (cipher.isEmpty()) {
        return QString();
    }

    DATA_BLOB inputBlob;
    inputBlob.pbData = reinterpret_cast<BYTE *>(const_cast<char *>(cipher.constData()));
    inputBlob.cbData = static_cast<DWORD>(cipher.size());

    DATA_BLOB outputBlob;
    outputBlob.pbData = nullptr;
    outputBlob.cbData = 0;

    if (!CryptUnprotectData(&inputBlob,
                            nullptr,
                            nullptr,
                            nullptr,
                            nullptr,
                            CRYPTPROTECT_UI_FORBIDDEN,
                            &outputBlob)) {
        return QString();
    }

    const QByteArray plain(reinterpret_cast<const char *>(outputBlob.pbData), outputBlob.cbData);
    LocalFree(outputBlob.pbData);
    return QString::fromUtf8(plain);
#else
    return QString::fromUtf8(qUncompress(QByteArray::fromBase64(cipherText.toLatin1())));
#endif
}
}

SubtitleTranslation::SubtitleTranslation(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::SubtitleTranslation),
    m_llmClient(new LlmServiceClient(this))
{
    ui->setupUi(this);

    initializePresetStorage();
    refreshPresetList();
    loadStoredSecrets();
    applyProviderDefaults(true);
    updateSecretInputState();

    ui->translateProgressBar->setRange(0, 100);
    ui->translateProgressBar->setValue(0);

    connect(ui->importPresetButton, &QPushButton::clicked, this, &SubtitleTranslation::importPresetToStorage);
    connect(ui->editPresetButton, &QPushButton::clicked, this, &SubtitleTranslation::openPromptEditingDialog);
    connect(ui->importSrtButton, &QPushButton::clicked, this, &SubtitleTranslation::importSrtFile);
    connect(ui->refreshModelButton, &QPushButton::clicked, this, &SubtitleTranslation::refreshRemoteModels);
    connect(ui->startTranslateButton, &QPushButton::clicked, this, &SubtitleTranslation::sendCommunicationProbe);

    connect(ui->providerComboBox,
            &QComboBox::currentTextChanged,
            this,
            [this](const QString &) { applyProviderDefaults(true); });

    connect(m_llmClient, &LlmServiceClient::modelsReady, this, &SubtitleTranslation::onModelsReady);
    connect(m_llmClient, &LlmServiceClient::chatCompleted, this, &SubtitleTranslation::onChatCompleted);
    connect(m_llmClient, &LlmServiceClient::requestFailed, this, &SubtitleTranslation::onRequestFailed);
    connect(m_llmClient, &LlmServiceClient::busyChanged, this, &SubtitleTranslation::onBusyChanged);
}

SubtitleTranslation::~SubtitleTranslation()
{
    delete ui;
}

void SubtitleTranslation::initializePresetStorage()
{
    m_presetDirectory = presetDirectoryPath();
    QDir().mkpath(m_presetDirectory);
}

void SubtitleTranslation::refreshPresetList(const QString &preferredPath)
{
    ui->presetComboBox->clear();

    QDir dir(m_presetDirectory);
    const QStringList files = dir.entryList(QStringList() << "*.json", QDir::Files, QDir::Name);
    for (const QString &fileName : files) {
        const QString fullPath = dir.filePath(fileName);
        ui->presetComboBox->addItem(fileName, fullPath);
    }

    if (ui->presetComboBox->count() == 0) {
        ui->presetComboBox->addItem(tr("未找到预设"), QString());
        return;
    }

    int targetIndex = 0;
    if (!preferredPath.isEmpty()) {
        const QString normalizedPreferred = QFileInfo(preferredPath).absoluteFilePath();
        for (int index = 0; index < ui->presetComboBox->count(); ++index) {
            const QString path = QFileInfo(ui->presetComboBox->itemData(index).toString()).absoluteFilePath();
            if (path == normalizedPreferred) {
                targetIndex = index;
                break;
            }
        }
    }
    ui->presetComboBox->setCurrentIndex(targetIndex);
}

QString SubtitleTranslation::selectedPresetPath() const
{
    const QVariant data = ui->presetComboBox->currentData();
    const QString dataPath = data.toString().trimmed();
    if (!dataPath.isEmpty()) {
        return dataPath;
    }

    const QString text = ui->presetComboBox->currentText().trimmed();
    if (text.isEmpty() || text == tr("未找到预设")) {
        return QString();
    }

    QString fileName = text;
    if (!fileName.endsWith(".json", Qt::CaseInsensitive)) {
        fileName += ".json";
    }
    return QDir(m_presetDirectory).filePath(fileName);
}

void SubtitleTranslation::importPresetToStorage()
{
    const QString sourcePath = QFileDialog::getOpenFileName(this,
                                                            tr("导入预设"),
                                                            QDir::homePath(),
                                                            tr("JSON 文件 (*.json)"));
    if (sourcePath.isEmpty()) {
        return;
    }

    const QFileInfo fileInfo(sourcePath);
    const QString destinationPath = QDir(m_presetDirectory).filePath(fileInfo.fileName());

    if (QFileInfo(sourcePath).absoluteFilePath() != QFileInfo(destinationPath).absoluteFilePath()) {
        if (QFile::exists(destinationPath)) {
            QFile::remove(destinationPath);
        }
        if (!QFile::copy(sourcePath, destinationPath)) {
            QMessageBox::warning(this, tr("导入失败"), tr("无法复制预设到目录：%1").arg(destinationPath));
            return;
        }
    }

    refreshPresetList(destinationPath);
}

void SubtitleTranslation::openPromptEditingDialog()
{
    QString presetPath = selectedPresetPath();
    if (!presetPath.isEmpty() && !QFileInfo::exists(presetPath)) {
        presetPath.clear();
    }

    PromptEditing dialog(m_presetDirectory, presetPath, this);
    if (dialog.exec() == QDialog::Accepted) {
        refreshPresetList(dialog.savedPresetPath());
    }
}

LlmServiceConfig SubtitleTranslation::collectServiceConfig()
{
    LlmServiceConfig config;
    config.provider = ui->providerComboBox->currentText().trimmed();
    config.baseUrl = ui->hostLineEdit->text().trimmed();
    config.apiKey = resolveSecretForRequest(secretApiKeyStorageKey(),
                                            ui->apiKeyLineEdit->text().trimmed(),
                                            &m_savedApiKey);
    config.serverPassword = resolveSecretForRequest(secretServerPasswordStorageKey(),
                                                    ui->serverPasswordLineEdit->text().trimmed(),
                                                    &m_savedServerPassword);
    config.model = ui->modelComboBox->currentText().trimmed();
    config.stream = ui->streamingCheckBox->isChecked();
    config.timeoutMs = 60000;

    updateSecretInputState();
    return config;
}

void SubtitleTranslation::applyProviderDefaults(bool forceResetHost)
{
    const QString provider = ui->providerComboBox->currentText().trimmed();
    const QString defaultHost = LlmServiceConfig::defaultBaseUrlForProvider(provider);

    if (forceResetHost || ui->hostLineEdit->text().trimmed().isEmpty()) {
        ui->hostLineEdit->setText(defaultHost);
    }

    const QString normalized = provider.toLower();
    if (normalized.contains("openai api")) {
        if (m_savedApiKey.isEmpty()) {
            ui->apiKeyLineEdit->setPlaceholderText(tr("首次输入后将加密保存"));
        } else {
            ui->apiKeyLineEdit->setPlaceholderText(tr("已加密保存，留空继续使用"));
        }
    } else {
        if (m_savedApiKey.isEmpty()) {
            ui->apiKeyLineEdit->setPlaceholderText(tr("可选，首次输入后将加密保存"));
        } else {
            ui->apiKeyLineEdit->setPlaceholderText(tr("已加密保存，留空继续使用"));
        }
    }
    if (m_savedServerPassword.isEmpty()) {
        ui->serverPasswordLineEdit->setPlaceholderText(tr("可选，首次输入后将加密保存"));
    } else {
        ui->serverPasswordLineEdit->setPlaceholderText(tr("已加密保存，留空继续使用"));
    }
}

void SubtitleTranslation::loadStoredSecrets()
{
    QSettings settings(QStringLiteral("qSrtTool"), QStringLiteral("qSrtTool"));
    m_savedApiKey = decryptSecret(settings.value(secretApiKeyStorageKey()).toString());
    m_savedServerPassword = decryptSecret(settings.value(secretServerPasswordStorageKey()).toString());
}

void SubtitleTranslation::updateSecretInputState()
{
    if (m_savedApiKey.isEmpty()) {
        ui->apiKeyLineEdit->setEchoMode(QLineEdit::Normal);
    } else {
        ui->apiKeyLineEdit->setEchoMode(QLineEdit::Password);
        ui->apiKeyLineEdit->clear();
    }

    if (m_savedServerPassword.isEmpty()) {
        ui->serverPasswordLineEdit->setEchoMode(QLineEdit::Normal);
    } else {
        ui->serverPasswordLineEdit->setEchoMode(QLineEdit::Password);
        ui->serverPasswordLineEdit->clear();
    }
}

void SubtitleTranslation::persistSecret(const QString &storageKey, const QString &plainSecret)
{
    QSettings settings(QStringLiteral("qSrtTool"), QStringLiteral("qSrtTool"));
    if (plainSecret.isEmpty()) {
        settings.remove(storageKey);
    } else {
        const QString encrypted = encryptSecret(plainSecret);
        if (!encrypted.isEmpty()) {
            settings.setValue(storageKey, encrypted);
        }
    }
    settings.sync();
}

QString SubtitleTranslation::resolveSecretForRequest(const QString &storageKey,
                                                    const QString &inputText,
                                                    QString *cachedSecret)
{
    if (!cachedSecret) {
        return QString();
    }

    if (!inputText.isEmpty()) {
        *cachedSecret = inputText;
        persistSecret(storageKey, *cachedSecret);
        return *cachedSecret;
    }

    return *cachedSecret;
}

void SubtitleTranslation::appendOutputMessage(const QString &message)
{
    const QString timestamp = QDateTime::currentDateTime().toString("HH:mm:ss");
    ui->outputTextEdit->append(QStringLiteral("[%1] %2").arg(timestamp, message));
}

void SubtitleTranslation::importSrtFile()
{
    const QString path = QFileDialog::getOpenFileName(this,
                                                      tr("导入 SRT"),
                                                      QDir::homePath(),
                                                      tr("字幕文件 (*.srt)"));
    if (path.isEmpty()) {
        return;
    }
    ui->srtPathLineEdit->setText(path);
}

void SubtitleTranslation::refreshRemoteModels()
{
    const LlmServiceConfig config = collectServiceConfig();
    if (!config.isValid()) {
        QMessageBox::warning(this, tr("配置错误"), tr("请先填写服务地址"));
        return;
    }

    ui->progressStatusLabel->setText(tr("正在拉取模型..."));
    ui->translateProgressBar->setRange(0, 0);
    appendOutputMessage(tr("开始请求模型列表：%1").arg(config.normalizedBaseUrl()));
    m_llmClient->requestModels(config);
}

void SubtitleTranslation::sendCommunicationProbe()
{
    const LlmServiceConfig config = collectServiceConfig();
    if (!config.isValid()) {
        QMessageBox::warning(this, tr("配置错误"), tr("请先填写服务地址"));
        return;
    }
    if (config.model.isEmpty()) {
        QMessageBox::warning(this, tr("配置错误"), tr("请先选择或输入模型名称"));
        return;
    }

    QJsonArray messages;
    QJsonObject systemMessage;
    systemMessage.insert("role", "system");
    systemMessage.insert("content", tr("You are a subtitle translation assistant."));
    messages.append(systemMessage);

    QJsonObject userMessage;
    userMessage.insert("role", "user");
    userMessage.insert("content", tr("请仅回复“连接成功”四个字。"));
    messages.append(userMessage);

    QJsonObject options;
    options.insert("temperature", ui->temperatureSpinBox->value());
    options.insert("max_tokens", ui->maxTokensSpinBox->value());

    ui->progressStatusLabel->setText(tr("正在发送通信测试..."));
    ui->translateProgressBar->setRange(0, 0);
    appendOutputMessage(tr("开始发送通信测试请求，模型：%1").arg(config.model));

    m_llmClient->requestChatCompletion(config, messages, options);
}

void SubtitleTranslation::onModelsReady(const QStringList &models)
{
    if (models.isEmpty()) {
        onRequestFailed(tr("模型列表"), tr("响应为空"));
        return;
    }

    const QString previousModel = ui->modelComboBox->currentText().trimmed();

    ui->modelComboBox->clear();
    ui->modelComboBox->addItems(models);

    const int previousIndex = ui->modelComboBox->findText(previousModel);
    if (previousIndex >= 0) {
        ui->modelComboBox->setCurrentIndex(previousIndex);
    }

    ui->translateProgressBar->setRange(0, 100);
    ui->translateProgressBar->setValue(100);
    ui->progressStatusLabel->setText(tr("模型列表已更新"));
    appendOutputMessage(tr("模型刷新成功，共 %1 个").arg(models.size()));
}

void SubtitleTranslation::onChatCompleted(const QString &content, const QJsonObject &)
{
    ui->translateProgressBar->setRange(0, 100);
    ui->translateProgressBar->setValue(100);
    ui->progressStatusLabel->setText(tr("通信测试成功"));
    appendOutputMessage(tr("服务响应：%1").arg(content));
}

void SubtitleTranslation::onRequestFailed(const QString &stage, const QString &message)
{
    ui->translateProgressBar->setRange(0, 100);
    ui->translateProgressBar->setValue(0);
    ui->progressStatusLabel->setText(tr("%1失败").arg(stage));
    appendOutputMessage(tr("%1失败：%2").arg(stage, message));
}

void SubtitleTranslation::onBusyChanged(bool busy)
{
    ui->refreshModelButton->setEnabled(!busy);
    ui->startTranslateButton->setEnabled(!busy);
}
