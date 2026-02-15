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
    Ui::SubtitleTranslation *ui;
};

#endif // SUBTITLETRANSLATION_H
