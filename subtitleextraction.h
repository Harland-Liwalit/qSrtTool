#ifndef SUBTITLEEXTRACTION_H
#define SUBTITLEEXTRACTION_H

#include <QWidget>

namespace Ui {
class SubtitleExtraction;
}

class SubtitleExtraction : public QWidget
{
    Q_OBJECT

public:
    explicit SubtitleExtraction(QWidget *parent = nullptr);
    ~SubtitleExtraction();

private:
    Ui::SubtitleExtraction *ui;
};

#endif // SUBTITLEEXTRACTION_H
