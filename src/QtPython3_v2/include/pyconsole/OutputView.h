#pragma once

#include <QPlainTextEdit>

/*
 * OutputView
 * ----------
 * Zone d'affichage de la sortie standard (noir) et de la sortie d'erreur
 * (rouge). Non éditable. Le texte des exceptions est inséré sous forme de
 * commentaire Python ("# ...") en rouge, au-dessus de la ligne concernée
 * dans l'éditeur — cette classe se contente d'afficher stdout/stderr.
 */
class OutputView : public QPlainTextEdit
{
    Q_OBJECT

public:
    explicit OutputView(QWidget* parent = nullptr);

public slots:
    void appendStdout(const QString& text);
    void appendStderr(const QString& text);
    void clearOutput();

private:
    void appendColored(const QString& text, const QColor& color);
};
