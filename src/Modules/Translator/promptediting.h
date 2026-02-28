#ifndef PROMPTEDITING_H
#define PROMPTEDITING_H

#include <QDialog>
#include <QJsonArray>
#include <QJsonObject>
#include <QMap>

namespace Ui {
class PromptEditing;
}

class PromptEditing : public QDialog
{
    Q_OBJECT

public:
    explicit PromptEditing(const QString &presetDirectory,
                           const QString &presetFilePath = QString(),
                           QWidget *parent = nullptr);
    ~PromptEditing();

    // 返回本次对话框保存成功的预设路径。
    QString savedPresetPath() const;

private:
    void setupUiBehavior();
    void setupConnections();

    void applyPresetToUi(const QJsonObject &presetObject);
    QJsonObject buildPresetFromUi();
    QJsonObject createDefaultPreset() const;

    // 读取指定预设文件并回填 UI。
    void loadPresetFromFile(const QString &filePath);
    // 新建默认预设草稿。
    void createNewPreset();
    // 导入外部 JSON 预设。
    void importPresetJson();
    // 导出当前预设为 JSON。
    void exportPresetJson();
    // 保存预设并关闭对话框。
    void savePresetAndAccept();

    void refreshPromptList();
    void loadPromptToEditor(int row);
    void commitPromptEditor();
    void addPrompt();
    void removePrompt();

    void loadOrderTableForCharacter(int characterId);
    void saveOrderTableForCurrentCharacter();
    QJsonArray orderTableToJsonArray() const;
    void applyOrderJsonArrayToTable(const QJsonArray &orderArray);
    void renumberOrderTable();
    void addOrderItem();
    void removeOrderItem();
    void applyManualOrder();

    // 同步并刷新右侧 JSON 预览。
    void refreshJsonPreview();
    QString sanitizeFileName(const QString &fileName) const;

    Ui::PromptEditing *ui;
    QString m_presetDirectory;
    QString m_currentFilePath;
    QString m_savedPresetPath;

    QJsonObject m_basePresetObject;
    QJsonArray m_prompts;
    QMap<int, QJsonArray> m_promptOrderByCharacter;
    int m_currentPromptRow = -1;
    int m_currentCharacterId = 100001;
    bool m_updatingUi = false;
};

#endif // PROMPTEDITING_H
