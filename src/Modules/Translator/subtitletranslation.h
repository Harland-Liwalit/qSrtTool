#ifndef SUBTITLETRANSLATION_H
#define SUBTITLETRANSLATION_H

#include <QWidget>

namespace Ui {
class SubtitleTranslation;
}

class SubtitleTranslation : public QWidget
{
    Q_OBJECT

public:
    explicit SubtitleTranslation(QWidget *parent = nullptr);
    ~SubtitleTranslation();

private:
    void initializePresetStorage();
    void refreshPresetList(const QString &preferredPath = QString());
    QString selectedPresetPath() const;

    void importPresetToStorage();
    void openPromptEditingDialog();

    Ui::SubtitleTranslation *ui;
    QString m_presetDirectory;
};

#endif // SUBTITLETRANSLATION_H
