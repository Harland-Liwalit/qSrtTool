#include "subtitletranslation.h"
#include "ui_subtitletranslation.h"

#include "llmserviceclient.h"
#include "promptrequestcomposer.h"
#include "promptediting.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QLineEdit>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSettings>
#include <QTextStream>
#include <QClipboard>

#include <algorithm>

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

QString naturalInstructionStorageKey()
{
    return QStringLiteral("translator/prompt/natural_instruction");
}

int segmentSize()
{
    return 200;
}

QString intermediateOutputDirectory()
{
    return QDir::currentPath() + "/temp/translator_intermediate";
}

QRegularExpression srtBlockRegex()
{
    static const QRegularExpression regex(
        QStringLiteral("(?ms)(\\d+)\\s*\\n\\s*(\\d{2}:\\d{2}:\\d{2}[,\\.]\\d{3})\\s*-->\\s*(\\d{2}:\\d{2}:\\d{2}[,\\.]\\d{3})\\s*\\n(.*?)(?=\\n{2,}\\d+\\s*\\n\\s*\\d{2}:\\d{2}:\\d{2}[,\\.]\\d{3}\\s*-->|\\z)"));
    return regex;
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
    loadStoredNaturalInstruction();
    applyProviderDefaults(true);
    updateSecretInputState();

    ui->modelComboBox->setEditable(true);

    ui->translateProgressBar->setRange(0, 100);
    ui->translateProgressBar->setValue(0);

    connect(ui->importPresetButton, &QPushButton::clicked, this, &SubtitleTranslation::importPresetToStorage);
    connect(ui->editPresetButton, &QPushButton::clicked, this, &SubtitleTranslation::openPromptEditingDialog);
    connect(ui->importSrtButton, &QPushButton::clicked, this, &SubtitleTranslation::importSrtFile);
    connect(ui->refreshModelButton, &QPushButton::clicked, this, &SubtitleTranslation::refreshRemoteModels);
    connect(ui->startTranslateButton, &QPushButton::clicked, this, &SubtitleTranslation::startSegmentedTranslation);
    connect(ui->exportSrtButton, &QPushButton::clicked, this, &SubtitleTranslation::onExportSrtClicked);
    connect(ui->copyResultButton, &QPushButton::clicked, this, &SubtitleTranslation::onCopyResultClicked);
    connect(ui->clearOutputButton, &QPushButton::clicked, this, &SubtitleTranslation::onClearOutputClicked);
        connect(ui->presetComboBox,
            static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            this,
            [this](int) { onPresetSelectionChanged(); });

        connect(ui->temperatureSpinBox,
            static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged),
            this,
            [this](double) { syncSharedParametersToPreset(); });
        connect(ui->streamingCheckBox, &QCheckBox::toggled, this, [this](bool) { syncSharedParametersToPreset(); });
        connect(ui->modelComboBox,
            &QComboBox::currentTextChanged,
            this,
            [this](const QString &) { syncSharedParametersToPreset(); });
        connect(ui->instructionTextEdit,
            &QTextEdit::textChanged,
            this,
            &SubtitleTranslation::onNaturalInstructionChanged);

    connect(ui->providerComboBox,
            &QComboBox::currentTextChanged,
            this,
            [this](const QString &) { applyProviderDefaults(true); });

    connect(m_llmClient, &LlmServiceClient::modelsReady, this, &SubtitleTranslation::onModelsReady);
    connect(m_llmClient, &LlmServiceClient::chatCompleted, this, &SubtitleTranslation::onChatCompleted);
    connect(m_llmClient, &LlmServiceClient::streamChunkReceived, this, &SubtitleTranslation::onStreamChunkReceived);
    connect(m_llmClient, &LlmServiceClient::requestFailed, this, &SubtitleTranslation::onRequestFailed);
    connect(m_llmClient, &LlmServiceClient::busyChanged, this, &SubtitleTranslation::onBusyChanged);

    applySharedPresetParameters();
    renderOutputPanel();
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

QJsonObject SubtitleTranslation::loadPresetObject(const QString &presetPath) const
{
    if (presetPath.trimmed().isEmpty() || !QFileInfo::exists(presetPath)) {
        return QJsonObject();
    }

    QFile file(presetPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QJsonObject();
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return QJsonObject();
    }

    return document.object();
}

bool SubtitleTranslation::savePresetObject(const QString &presetPath, const QJsonObject &presetObject) const
{
    if (presetPath.trimmed().isEmpty()) {
        return false;
    }

    QFile file(presetPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        return false;
    }

    const QJsonDocument document(presetObject);
    return file.write(document.toJson(QJsonDocument::Indented)) >= 0;
}

void SubtitleTranslation::applySharedPresetParameters()
{
    const QString presetPath = selectedPresetPath();
    const QJsonObject presetObject = loadPresetObject(presetPath);
    if (presetObject.isEmpty()) {
        return;
    }

    m_syncingSharedParameters = true;

    if (presetObject.contains("temperature")) {
        ui->temperatureSpinBox->setValue(presetObject.value("temperature").toDouble(ui->temperatureSpinBox->value()));
    }
    if (presetObject.contains("stream_openai")) {
        ui->streamingCheckBox->setChecked(presetObject.value("stream_openai").toBool(ui->streamingCheckBox->isChecked()));
    }

    QString model = presetObject.value("custom_model").toString().trimmed();
    if (model.isEmpty()) {
        model = presetObject.value("openrouter_model").toString().trimmed();
    }
    if (!model.isEmpty()) {
        ui->modelComboBox->setCurrentText(model);
    }

    m_syncingSharedParameters = false;
}

void SubtitleTranslation::syncSharedParametersToPreset()
{
    if (m_syncingSharedParameters) {
        return;
    }

    const QString presetPath = selectedPresetPath();
    if (presetPath.trimmed().isEmpty() || !QFileInfo::exists(presetPath)) {
        return;
    }

    QJsonObject presetObject = loadPresetObject(presetPath);
    if (presetObject.isEmpty()) {
        return;
    }

    presetObject.insert("temperature", ui->temperatureSpinBox->value());
    presetObject.insert("stream_openai", ui->streamingCheckBox->isChecked());
    presetObject.insert("custom_model", ui->modelComboBox->currentText().trimmed());

    savePresetObject(presetPath, presetObject);
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
        applySharedPresetParameters();
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

void SubtitleTranslation::loadStoredNaturalInstruction()
{
    QSettings settings(QStringLiteral("qSrtTool"), QStringLiteral("qSrtTool"));
    const QString text = settings.value(naturalInstructionStorageKey()).toString().trimmed();
    if (!text.isEmpty()) {
        ui->instructionTextEdit->setPlainText(text);
    }
}

void SubtitleTranslation::persistNaturalInstruction()
{
    QSettings settings(QStringLiteral("qSrtTool"), QStringLiteral("qSrtTool"));
    settings.setValue(naturalInstructionStorageKey(), ui->instructionTextEdit->toPlainText().trimmed());
    settings.sync();
}

QString SubtitleTranslation::buildAutoInstructionText() const
{
    QString instruction = ui->instructionTextEdit->toPlainText().trimmed();
    if (instruction.isEmpty()) {
        instruction = tr("这是一个影视字幕任务，请翻译成%1，注意术语统一、语气自然，并遵循预设规则。")
                          .arg(ui->targetLangComboBox->currentText().trimmed());
    }
    return instruction;
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
    m_outputLogLines.append(QStringLiteral("[%1] %2").arg(timestamp, message));
    if (m_outputLogLines.size() > 500) {
        m_outputLogLines.removeFirst();
    }
    renderOutputPanel();
}

void SubtitleTranslation::renderOutputPanel()
{
    QStringList blocks;
    blocks << QStringLiteral("【输出预览】");
    blocks << (m_outputPreviewText.trimmed().isEmpty() ? tr("(暂无预览内容)") : m_outputPreviewText.trimmed());
    blocks << QString();
    blocks << QStringLiteral("【日志】");
    if (m_outputLogLines.isEmpty()) {
        blocks << tr("(暂无日志)");
    } else {
        blocks << m_outputLogLines;
    }

    ui->outputTextEdit->setPlainText(blocks.join('\n'));
    ui->outputTextEdit->verticalScrollBar()->setValue(ui->outputTextEdit->verticalScrollBar()->maximum());
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
    startSegmentedTranslation();
}

QString SubtitleTranslation::normalizeTimelineToken(const QString &timelineToken) const
{
    QString value = timelineToken.trimmed();
    value.replace('.', ',');
    return value;
}

qint64 SubtitleTranslation::timelineToMs(const QString &timelineToken) const
{
    const QString normalized = normalizeTimelineToken(timelineToken);
    const QRegularExpression tokenRegex(QStringLiteral(R"((\d{2}):(\d{2}):(\d{2}),(\d{3}))"));
    const QRegularExpressionMatch match = tokenRegex.match(normalized);
    if (!match.hasMatch()) {
        return -1;
    }

    const int hours = match.captured(1).toInt();
    const int minutes = match.captured(2).toInt();
    const int seconds = match.captured(3).toInt();
    const int millis = match.captured(4).toInt();
    return (((hours * 60LL + minutes) * 60LL) + seconds) * 1000LL + millis;
}

QString SubtitleTranslation::msToTimeline(qint64 ms) const
{
    if (ms < 0) {
        ms = 0;
    }

    const qint64 hours = ms / 3600000;
    ms %= 3600000;
    const qint64 minutes = ms / 60000;
    ms %= 60000;
    const qint64 seconds = ms / 1000;
    ms %= 1000;

    return QStringLiteral("%1:%2:%3,%4")
        .arg(hours, 2, 10, QChar('0'))
        .arg(minutes, 2, 10, QChar('0'))
        .arg(seconds, 2, 10, QChar('0'))
        .arg(ms, 3, 10, QChar('0'));
}

QVector<SubtitleTranslation::SubtitleEntry> SubtitleTranslation::parseSrtEntries(const QString &srtText) const
{
    QVector<SubtitleEntry> entries;
    QRegularExpressionMatchIterator iterator = srtBlockRegex().globalMatch(srtText);
    while (iterator.hasNext()) {
        const QRegularExpressionMatch match = iterator.next();
        SubtitleEntry entry;
        entry.index = match.captured(1).trimmed().toInt();
        entry.startText = normalizeTimelineToken(match.captured(2));
        entry.endText = normalizeTimelineToken(match.captured(3));
        entry.startMs = timelineToMs(entry.startText);
        entry.endMs = timelineToMs(entry.endText);
        entry.text = match.captured(4).trimmed();

        if (entry.startMs < 0 || entry.endMs < 0 || entry.text.isEmpty()) {
            continue;
        }
        entries.append(entry);
    }
    return entries;
}

QString SubtitleTranslation::serializeSrtEntries(const QVector<SubtitleEntry> &entries, bool reindex) const
{
    QStringList blocks;
    blocks.reserve(entries.size());

    for (int i = 0; i < entries.size(); ++i) {
        const SubtitleEntry &entry = entries.at(i);
        const int number = reindex ? (i + 1) : (entry.index > 0 ? entry.index : (i + 1));
        const QString startToken = entry.startText.isEmpty() ? msToTimeline(entry.startMs) : normalizeTimelineToken(entry.startText);
        const QString endToken = entry.endText.isEmpty() ? msToTimeline(entry.endMs) : normalizeTimelineToken(entry.endText);

        QString block;
        QTextStream stream(&block);
        stream << number << '\n';
        stream << startToken << " --> " << endToken << '\n';
        stream << entry.text.trimmed();
        blocks.append(block);
    }

    return blocks.join(QStringLiteral("\n\n"));
}

QString SubtitleTranslation::cleanSrtPreviewText(const QString &rawText) const
{
    const QVector<SubtitleEntry> parsed = parseSrtEntries(rawText);
    if (parsed.isEmpty()) {
        return rawText.trimmed();
    }
    return serializeSrtEntries(parsed, true);
}

void SubtitleTranslation::resetTranslationSessionState()
{
    m_sourceEntries.clear();
    m_segments.clear();
    m_translatedByStartMs.clear();
    m_currentSegment = -1;
    m_waitingExportToContinue = false;
    m_currentSegmentRawResponse.clear();
    m_currentSegmentCleanPreview.clear();
    m_lastFinalMergedSrt.clear();
    m_exportTargetPath.clear();
}

void SubtitleTranslation::startSegmentedTranslation()
{
    syncSharedParametersToPreset();

    const LlmServiceConfig config = collectServiceConfig();
    if (!config.isValid()) {
        QMessageBox::warning(this, tr("配置错误"), tr("请先填写服务地址"));
        return;
    }
    if (config.model.isEmpty()) {
        QMessageBox::warning(this, tr("配置错误"), tr("请先选择或输入模型名称"));
        return;
    }

    const QString srtPath = ui->srtPathLineEdit->text().trimmed();
    if (srtPath.isEmpty() || !QFileInfo::exists(srtPath)) {
        QMessageBox::warning(this, tr("输入错误"), tr("请先导入有效的 SRT 文件"));
        return;
    }

    QFile srtFile(srtPath);
    if (!srtFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("读取失败"), tr("无法打开 SRT 文件：%1").arg(srtPath));
        return;
    }

    const QString srtContent = QString::fromUtf8(srtFile.readAll());
    const QVector<SubtitleEntry> sourceEntries = parseSrtEntries(srtContent);
    if (sourceEntries.isEmpty()) {
        QMessageBox::warning(this, tr("解析失败"), tr("未解析到可用字幕条目，请检查 SRT 格式"));
        return;
    }

    const QString presetPath = selectedPresetPath();
    const QJsonObject presetObject = loadPresetObject(presetPath);

    PromptComposeInput composeInput;
    composeInput.naturalInstruction = buildAutoInstructionText();
    composeInput.sourceLanguage = ui->sourceLangComboBox->currentText().trimmed();
    composeInput.targetLanguage = ui->targetLangComboBox->currentText().trimmed();
    composeInput.keepTimeline = ui->keepTimelineCheckBox->isChecked();
    composeInput.reviewPolish = ui->reviewCheckBox->isChecked();
    composeInput.srtPath = srtPath;
    if (!presetObject.isEmpty()) {
        composeInput.presetJson = QString::fromUtf8(QJsonDocument(presetObject).toJson(QJsonDocument::Indented));
    }

    QJsonObject options;
    options.insert("temperature", ui->temperatureSpinBox->value());
    options.insert("max_tokens", ui->maxTokensSpinBox->value());

    resetTranslationSessionState();
    m_sourceEntries = sourceEntries;
    m_activeConfig = config;
    m_activeOptions = options;
    m_activeComposeInput = composeInput;

    const int chunk = qMax(1, segmentSize());
    for (int offset = 0; offset < m_sourceEntries.size(); offset += chunk) {
        const int count = qMin(chunk, m_sourceEntries.size() - offset);
        QVector<SubtitleEntry> segment;
        segment.reserve(count);
        for (int i = 0; i < count; ++i) {
            segment.append(m_sourceEntries.at(offset + i));
        }
        m_segments.append(segment);
    }

    m_currentSegment = 0;
    m_outputLogLines.clear();
    m_outputPreviewText.clear();
    appendOutputMessage(tr("已解析字幕 %1 条，按每段 %2 条分为 %3 段")
                        .arg(m_sourceEntries.size())
                        .arg(chunk)
                        .arg(m_segments.size()));

    sendCurrentSegmentRequest();
}

QVector<SubtitleTranslation::SubtitleEntry> SubtitleTranslation::currentSegmentSourceEntries() const
{
    if (m_currentSegment < 0 || m_currentSegment >= m_segments.size()) {
        return QVector<SubtitleEntry>();
    }
    return m_segments.at(m_currentSegment);
}

QString SubtitleTranslation::buildSegmentPromptSrt(const QVector<SubtitleEntry> &entries) const
{
    return serializeSrtEntries(entries, false);
}

void SubtitleTranslation::sendCurrentSegmentRequest()
{
    if (m_currentSegment < 0 || m_currentSegment >= m_segments.size()) {
        return;
    }

    const QVector<SubtitleEntry> segmentEntries = currentSegmentSourceEntries();
    const QString segmentSrt = buildSegmentPromptSrt(segmentEntries);

    QJsonArray messages;
    QJsonObject instructionMessage;
    instructionMessage.insert("role", "user");
    instructionMessage.insert("content",
                              PromptRequestComposer::buildFinalInstruction(m_activeComposeInput)
                                  + "\n\n请严格输出 SRT 格式，仅返回字幕条目，不要额外解释。"
                                  + "\n若某条是噪声可省略，但保留其余条目的原时间戳。");
    messages.append(instructionMessage);

    QJsonObject segmentMessage;
    segmentMessage.insert("role", "user");
    segmentMessage.insert("content",
                          tr("【待翻译分段 %1/%2】\n%3")
                              .arg(m_currentSegment + 1)
                              .arg(m_segments.size())
                              .arg(segmentSrt));
    messages.append(segmentMessage);

    m_currentSegmentRawResponse.clear();
    m_currentSegmentCleanPreview.clear();
    m_waitingExportToContinue = false;

    ui->progressStatusLabel->setText(tr("正在翻译第 %1/%2 段...").arg(m_currentSegment + 1).arg(m_segments.size()));
    ui->translateProgressBar->setRange(0, 0);
    appendOutputMessage(tr("开始发送第 %1 段翻译请求（%2 条）")
                        .arg(m_currentSegment + 1)
                        .arg(segmentEntries.size()));
    m_llmClient->requestChatCompletion(m_activeConfig, messages, m_activeOptions);
}

void SubtitleTranslation::updateLivePreview(const QString &rawResponse)
{
    m_currentSegmentRawResponse = rawResponse;
    const QString cleaned = cleanSrtPreviewText(rawResponse);
    if (cleaned == m_currentSegmentCleanPreview) {
        return;
    }

    m_currentSegmentCleanPreview = cleaned;
    m_outputPreviewText = cleaned;
    renderOutputPanel();
}

void SubtitleTranslation::applySegmentTranslationResult(const QString &rawResponse)
{
    updateLivePreview(rawResponse);

    const QVector<SubtitleEntry> translated = parseSrtEntries(m_currentSegmentCleanPreview);
    const QVector<SubtitleEntry> segmentSource = currentSegmentSourceEntries();

    if (!translated.isEmpty()) {
        for (const SubtitleEntry &translatedEntry : translated) {
            SubtitleEntry mergedEntry = translatedEntry;
            if (mergedEntry.startText.isEmpty()) {
                mergedEntry.startText = msToTimeline(mergedEntry.startMs);
            }
            if (mergedEntry.endText.isEmpty()) {
                mergedEntry.endText = msToTimeline(mergedEntry.endMs);
            }
            m_translatedByStartMs.insert(mergedEntry.startMs, mergedEntry);
        }
    } else {
        const QVector<SubtitleEntry> fallback = parseSrtEntries(rawResponse);
        for (const SubtitleEntry &entry : fallback) {
            m_translatedByStartMs.insert(entry.startMs, entry);
        }
    }

    const int progress = qRound(((m_currentSegment + 1.0) / qMax(1, m_segments.size())) * 100.0);
    ui->translateProgressBar->setRange(0, 100);
    ui->translateProgressBar->setValue(progress);
    ui->progressStatusLabel->setText(tr("第 %1 段翻译完成，等待导出继续").arg(m_currentSegment + 1));

    appendOutputMessage(tr("第 %1 段返回完成：输入 %2 条，解析输出 %3 条。点击“导出 SRT”将生成中间文件并继续下一段。")
                        .arg(m_currentSegment + 1)
                        .arg(segmentSource.size())
                        .arg(translated.size()));
    m_waitingExportToContinue = true;
}

bool SubtitleTranslation::prepareExportTargetPath()
{
    if (!m_exportTargetPath.isEmpty()) {
        return true;
    }

    const QString defaultPath = QDir::homePath() + "/translated_output.srt";
    const QString selectedPath = QFileDialog::getSaveFileName(this,
                                                              tr("导出 SRT"),
                                                              defaultPath,
                                                              tr("字幕文件 (*.srt)"));
    if (selectedPath.isEmpty()) {
        return false;
    }
    m_exportTargetPath = selectedPath;
    return true;
}

void SubtitleTranslation::writeCurrentSegmentIntermediateFile()
{
    if (m_currentSegment < 0) {
        return;
    }

    const QVector<SubtitleEntry> translated = parseSrtEntries(m_currentSegmentCleanPreview);
    if (translated.isEmpty()) {
        appendOutputMessage(tr("第 %1 段未生成可写入的中间 SRT，跳过中间文件输出").arg(m_currentSegment + 1));
        return;
    }

    const QString dirPath = intermediateOutputDirectory();
    QDir().mkpath(dirPath);
    const QString fileName = QStringLiteral("segment_%1.srt").arg(m_currentSegment + 1, 3, 10, QChar('0'));
    const QString filePath = QDir(dirPath).filePath(fileName);

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        appendOutputMessage(tr("中间文件写入失败：%1").arg(filePath));
        return;
    }

    const QByteArray payload = serializeSrtEntries(translated, true).toUtf8();
    file.write(payload);
    file.close();

    appendOutputMessage(tr("已生成中间文件：%1").arg(filePath));
}

QVector<SubtitleTranslation::SubtitleEntry> SubtitleTranslation::mergedTranslatedEntriesByTimestamp() const
{
    QVector<SubtitleEntry> merged;
    merged.reserve(m_translatedByStartMs.size());

    for (auto it = m_translatedByStartMs.constBegin(); it != m_translatedByStartMs.constEnd(); ++it) {
        merged.append(it.value());
    }

    std::sort(merged.begin(), merged.end(), [](const SubtitleEntry &a, const SubtitleEntry &b) {
        if (a.startMs == b.startMs) {
            return a.endMs < b.endMs;
        }
        return a.startMs < b.startMs;
    });
    return merged;
}

void SubtitleTranslation::exportFinalMergedSrt()
{
    if (!prepareExportTargetPath()) {
        return;
    }

    const QVector<SubtitleEntry> mergedEntries = mergedTranslatedEntriesByTimestamp();
    if (mergedEntries.isEmpty()) {
        appendOutputMessage(tr("尚无可导出的翻译内容"));
        return;
    }

    const QString mergedSrt = serializeSrtEntries(mergedEntries, true);
    QFile outFile(m_exportTargetPath);
    if (!outFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        QMessageBox::warning(this, tr("导出失败"), tr("无法写入文件：%1").arg(m_exportTargetPath));
        return;
    }

    outFile.write(mergedSrt.toUtf8());
    outFile.close();

    m_lastFinalMergedSrt = mergedSrt;
    m_outputPreviewText = mergedSrt;
    renderOutputPanel();

    ui->progressStatusLabel->setText(tr("全部分段完成，已按时间戳合并导出"));
    ui->translateProgressBar->setRange(0, 100);
    ui->translateProgressBar->setValue(100);
    appendOutputMessage(tr("导出完成：%1（共 %2 条，按时间戳顺序合并）")
                        .arg(m_exportTargetPath)
                        .arg(mergedEntries.size()));
}

void SubtitleTranslation::onExportSrtClicked()
{
    if (m_currentSegment < 0) {
        if (!m_lastFinalMergedSrt.trimmed().isEmpty()) {
            exportFinalMergedSrt();
        } else {
            appendOutputMessage(tr("当前没有进行中的翻译任务"));
        }
        return;
    }

    if (!m_waitingExportToContinue) {
        appendOutputMessage(tr("当前分段尚未返回，暂不能导出"));
        return;
    }

    if (!prepareExportTargetPath()) {
        return;
    }

    writeCurrentSegmentIntermediateFile();
    m_waitingExportToContinue = false;

    ++m_currentSegment;
    if (m_currentSegment < m_segments.size()) {
        appendOutputMessage(tr("继续发送第 %1 段翻译请求").arg(m_currentSegment + 1));
        sendCurrentSegmentRequest();
        return;
    }

    m_currentSegment = -1;
    exportFinalMergedSrt();
}

void SubtitleTranslation::onCopyResultClicked()
{
    const QString content = m_outputPreviewText.trimmed();
    if (content.isEmpty()) {
        appendOutputMessage(tr("暂无可复制的输出内容"));
        return;
    }

    if (QClipboard *clipboard = QGuiApplication::clipboard()) {
        clipboard->setText(content);
        appendOutputMessage(tr("已复制当前预览内容到剪贴板"));
    }
}

void SubtitleTranslation::onClearOutputClicked()
{
    m_outputPreviewText.clear();
    m_outputLogLines.clear();
    renderOutputPanel();
}

void SubtitleTranslation::onPresetSelectionChanged()
{
    applySharedPresetParameters();
}

void SubtitleTranslation::onNaturalInstructionChanged()
{
    persistNaturalInstruction();
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
    if (m_currentSegment >= 0) {
        applySegmentTranslationResult(content);
        return;
    }

    m_outputPreviewText = cleanSrtPreviewText(content);
    renderOutputPanel();
    ui->translateProgressBar->setRange(0, 100);
    ui->translateProgressBar->setValue(100);
    ui->progressStatusLabel->setText(tr("请求完成"));
    appendOutputMessage(tr("服务响应完成"));
}

void SubtitleTranslation::onStreamChunkReceived(const QString &, const QString &aggregatedContent)
{
    if (m_currentSegment < 0) {
        return;
    }

    updateLivePreview(aggregatedContent);
    ui->progressStatusLabel->setText(tr("第 %1/%2 段流式返回中...").arg(m_currentSegment + 1).arg(m_segments.size()));
}

void SubtitleTranslation::onRequestFailed(const QString &stage, const QString &message)
{
    m_waitingExportToContinue = false;
    ui->translateProgressBar->setRange(0, 100);
    ui->translateProgressBar->setValue(0);
    ui->progressStatusLabel->setText(tr("%1失败").arg(stage));
    appendOutputMessage(tr("%1失败：%2").arg(stage, message));
}

void SubtitleTranslation::onBusyChanged(bool busy)
{
    ui->refreshModelButton->setEnabled(!busy);
    ui->startTranslateButton->setEnabled(!busy);
    ui->exportSrtButton->setEnabled(!busy || m_waitingExportToContinue);
}
