#include "pyconsole/CodeEditor.h"

#include <QPainter>
#include <QTextBlock>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QMimeData>
#include <QScrollBar>
#include <QCoreApplication>
#include <QTextDocument>
#include <QRegularExpression>

namespace {
// Couleurs (cf. cahier des charges)
const QColor kExecutedBg(230, 230, 230);      // gris clair
const QColor kExecutedFg(0, 0, 128);          // bleu marine
const QColor kNotExecutedBg(255, 255, 255);   // blanc
const QColor kNotExecutedFg(0, 0, 0);         // noir
const QColor kArrowColor(255, 140, 0);        // orange
const QColor kBreakpointColor(200, 0, 0);     // rouge
const QColor kErrorLineBg(255, 200, 200);     // rouge clair
}

// Petit widget interne servant de gouttière (numéros de ligne, flèche,
// points d'arrêt). Un clic dans la gouttière bascule le point d'arrêt de
// la ligne correspondante (si elle appartient au code non exécuté).
class LineNumberArea : public QWidget
{
public:
    explicit LineNumberArea(CodeEditor* editor) : QWidget(editor), m_editor(editor) {}

    QSize sizeHint() const override
    {
        return QSize(m_editor->lineNumberAreaWidth(), 0);
    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        m_editor->lineNumberAreaPaintEvent(event);
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        m_editor->handleGutterClick(event->pos().y());
    }

private:
    CodeEditor* m_editor;
};

CodeEditor::CodeEditor(QWidget* parent)
    : QPlainTextEdit(parent)
{
    m_lineNumberArea = new LineNumberArea(this);

    connect(this, &QPlainTextEdit::blockCountChanged, this, &CodeEditor::updateLineNumberAreaWidth);
    connect(this, &QPlainTextEdit::updateRequest, this, &CodeEditor::updateLineNumberArea);
    connect(document(), &QTextDocument::contentsChange, this, &CodeEditor::onContentsChange);
    m_lastKnownBlockCount = blockCount();

    updateLineNumberAreaWidth(0);

    QFont f("Monospace");
    f.setStyleHint(QFont::TypeWriter);
    setFont(f);
    setTabStopDistance(4 * fontMetrics().horizontalAdvance(' '));
    setLineWrapMode(QPlainTextEdit::NoWrap);

    m_completionPopup = new QListWidget(this);
    m_completionPopup->setWindowFlags(Qt::ToolTip);
    m_completionPopup->hide();
    connect(m_completionPopup, &QListWidget::itemActivated, this, [this](QListWidgetItem* item) {
        insertCompletion(item->text());
    });
}

int CodeEditor::lineNumberAreaWidth() const
{
    int digits = 1;
    int maxLines = qMax(1, blockCount());
    while (maxLines >= 10) { maxLines /= 10; ++digits; }
    // marge pour : numéro + point d'arrêt + flèche
    return 24 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits;
}

void CodeEditor::updateLineNumberAreaWidth(int)
{
    setViewportMargins(lineNumberAreaWidth(), 0, 0, 0);
}

void CodeEditor::updateLineNumberArea(const QRect& rect, int dy)
{
    if (dy)
        m_lineNumberArea->scroll(0, dy);
    else
        m_lineNumberArea->update(0, rect.y(), m_lineNumberArea->width(), rect.height());

    if (rect.contains(viewport()->rect()))
        updateLineNumberAreaWidth(0);
}

void CodeEditor::resizeEvent(QResizeEvent* event)
{
    QPlainTextEdit::resizeEvent(event);
    QRect cr = contentsRect();
    m_lineNumberArea->setGeometry(QRect(cr.left(), cr.top(), lineNumberAreaWidth(), cr.height()));
}

void CodeEditor::lineNumberAreaPaintEvent(QPaintEvent* event)
{
    QPainter painter(m_lineNumberArea);
    painter.fillRect(event->rect(), QColor(245, 245, 245));

    QTextBlock block = firstVisibleBlock();
    int blockNumber = block.blockNumber();
    int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
    int bottom = top + qRound(blockBoundingRect(block).height());

    while (block.isValid() && top <= event->rect().bottom()) {
        if (block.isVisible() && bottom >= event->rect().top()) {
            const int lineNumber = blockNumber + 1; // 1-based
            const QString number = QString::number(lineNumber);

            painter.setPen(Qt::darkGray);
            painter.drawText(0, top, m_lineNumberArea->width() - 18,
                              fontMetrics().height(), Qt::AlignRight, number);

            if (m_breakpoints.contains(lineNumber)) {
                painter.setBrush(kBreakpointColor);
                painter.setPen(Qt::NoPen);
                const int d = fontMetrics().height() / 2;
                painter.drawEllipse(QPoint(m_lineNumberArea->width() - 10, top + fontMetrics().height() / 2), d / 2, d / 2);
            }

            if (lineNumber == m_currentExecLine) {
                painter.setPen(QPen(kArrowColor, 2));
                painter.setBrush(kArrowColor);
                const int y = top + fontMetrics().height() / 2;
                const int x = 2;
                QPolygon arrow;
                arrow << QPoint(x, y - 5) << QPoint(x, y + 5) << QPoint(x + 8, y);
                painter.drawPolygon(arrow);
            }
        }
        block = block.next();
        top = bottom;
        bottom = top + qRound(blockBoundingRect(block).height());
        ++blockNumber;
    }
}

void CodeEditor::setExecutedLineCount(int count)
{
    m_executedLineCount = qMax(0, count);
    applyHighlighting();
}

void CodeEditor::setCurrentExecLine(int line)
{
    m_currentExecLine = line;
    m_lineNumberArea->update();
    applyHighlighting();
}

void CodeEditor::setErrorLine(int line)
{
    m_errorLine = line;
    applyHighlighting();
}

void CodeEditor::setErrorCommentLines(int line, int count)
{
    m_errorCommentLine = line;
    m_errorCommentLineCount = qMax(1, count);
    applyHighlighting();
}

void CodeEditor::setBreakpointMarks(const QSet<int>& lines)
{
    m_breakpoints = lines;
    m_lineNumberArea->update();
}

void CodeEditor::setExecuting(bool executing)
{
    m_executing = executing;
    if (!executing) {
        m_paused = false; // pas de pause hors exécution
    }
    // Pendant l'exécution, tout le document est protégé (y compris le code
    // non encore exécuté), conformément au cahier des charges.
}

void CodeEditor::setPaused(bool paused)
{
    m_paused = paused;
}

void CodeEditor::onContentsChange(int position, int charsRemoved, int charsAdded)
{
    Q_UNUSED(charsRemoved);
    Q_UNUSED(charsAdded);

    const int newBlockCount = document()->blockCount();
    const int delta = newBlockCount - m_lastKnownBlockCount;
    m_lastKnownBlockCount = newBlockCount;

    if (delta == 0) {
        return; // pas de ligne insérée/supprimée (simple modification de texte)
    }

    // Ligne (1-based) où l'édition a débuté, et si elle a eu lieu très
    // exactement au tout début de cette ligne.
    QTextCursor posCursor(document());
    const int clampedPos = qBound(0, position, qMax(0, document()->characterCount() - 1));
    posCursor.setPosition(clampedPos);
    const int editLine = posCursor.blockNumber() + 1;
    const bool editAtBlockStart = posCursor.atBlockStart();

    // Calcule la nouvelle position d'une ligne (1-based) après le
    // décalage : inchangée si avant la zone éditée, décalée de `delta` si
    // après (insertion ou suppression), ou 0 (= "n'existe plus") si elle
    // tombait exactement dans une zone supprimée.
    auto shiftLine = [&](int line) -> int {
        if (line <= 0)
            return 0;
        if (delta > 0) {
            // Insertion : si elle a lieu exactement au tout début de la
            // ligne "editLine", cette ligne (et tout ce qui suit) descend
            // avec le nouveau contenu inséré avant elle. Sinon (édition
            // au milieu ou en fin de ligne -- ex: touche Entrée en fin de
            // ligne pour créer une nouvelle ligne dessous), la ligne
            // "editLine" elle-même NE bouge PAS : son instruction est
            // toujours là, seul ce qui la suit est décalé. Sans cette
            // distinction, un simple retour à la ligne en fin de ligne
            // décalait à tort un point d'arrêt posé sur cette même ligne.
            const int cutoff = editAtBlockStart ? editLine : editLine + 1;
            if (line < cutoff)
                return line;
            return line + delta;
        }
        // Suppression : même distinction que pour l'insertion ci-dessus.
        // Si elle démarre exactement au tout début de "editLine" (ex:
        // sélection de plusieurs lignes complètes depuis le début de la
        // première), "editLine" fait elle-même partie de la zone
        // potentiellement supprimée. Sinon (ex: touche Retour arrière en
        // tout début de ligne, qui fusionne la ligne courante dans la
        // PRÉCÉDENTE -- "editLine" ici), "editLine" SURVIT intacte : son
        // propre contenu n'est pas perdu, seule la séparation avec la
        // ligne suivante disparaît. Sans cette distinction, un point
        // d'arrêt posé sur la ligne qui "reçoit" la fusion (via Retour
        // arrière en début de ligne suivante) était supprimé à tort.
        const int deletedCount = -delta;
        if (editAtBlockStart) {
            if (line < editLine)
                return line;
            if (line < editLine + deletedCount) {
                return 0; // l'instruction associée a été supprimée
            }
            return line + delta;
        } else {
            if (line <= editLine)
                return line;
            if (line <= editLine + deletedCount) {
                return 0; // l'instruction associée a été supprimée
            }
            return line + delta;
        }
    };

    // Points d'arrêt : décalés pour rester en face de leur instruction, ou
    // supprimés si celle-ci a été effacée.
    bool breakpointsChanged = false;
    QSet<int> updatedBreakpoints;
    for (int bp : qAsConst(m_breakpoints)) {
        const int shifted = shiftLine(bp);
        if (shifted != bp) {
            breakpointsChanged = true;
        }
        if (shifted > 0) {
            updatedBreakpoints.insert(shifted);
        }
    }
    if (breakpointsChanged) {
        m_breakpoints = updatedBreakpoints;
    }

    // Flèche d'instruction courante / ligne fautive / commentaire
    // d'exception : mêmes règles, pour rester cohérents visuellement avec
    // le code après édition.
    bool visualChanged = false;
    const int shiftedExec = shiftLine(m_currentExecLine);
    if (shiftedExec != m_currentExecLine) { m_currentExecLine = shiftedExec; visualChanged = true; }
    const int shiftedError = shiftLine(m_errorLine);
    if (shiftedError != m_errorLine) { m_errorLine = shiftedError; visualChanged = true; }
    const int shiftedComment = shiftLine(m_errorCommentLine);
    if (shiftedComment != m_errorCommentLine) { m_errorCommentLine = shiftedComment; visualChanged = true; }

    // La flèche d'instruction courante, la ligne fautive et le point
    // d'arrêt sont tous dessinés dans la gouttière (lineNumberAreaPaintEvent) :
    // il faut la repeindre dès que l'un ou l'autre change, pas seulement
    // les points d'arrêt -- sans quoi la flèche reste visuellement figée à
    // son ancienne position bien que sa valeur interne (m_currentExecLine)
    // ait été correctement mise à jour.
    if (visualChanged || breakpointsChanged) {
        m_lineNumberArea->update();
        applyHighlighting();
    }
}

void CodeEditor::applyHighlighting()
{
    QList<QTextEdit::ExtraSelection> selections;

    const int total = blockCount();
    for (int i = 0; i < total; ++i) {
        const int lineNumber = i + 1;
        QTextBlock block = document()->findBlockByNumber(i);
        if (!block.isValid())
            continue;

        QTextEdit::ExtraSelection sel;
        sel.cursor = QTextCursor(block);
        sel.cursor.select(QTextCursor::LineUnderCursor);

        const bool isErrorComment = (m_errorCommentLine > 0
                && lineNumber >= m_errorCommentLine
                && lineNumber < m_errorCommentLine + m_errorCommentLineCount);

        if (isErrorComment) {
            // Commentaire d'exception : texte rouge sur fond neutre (ce
            // n'est ni du code exécuté, ni la ligne fautive elle-même).
            // Calculé ici, dans la MÊME ExtraSelection que le fond/texte
            // normal (plutôt que dans une seconde sélection superposée sur
            // la même plage), pour un rendu déterministe : empiler deux
            // ExtraSelection sur une plage identique ne garantit pas que
            // le format de la seconde l'emporte sur celui de la première
            // pour la couleur du texte.
            sel.format.setForeground(QColor(200, 0, 0));
            sel.format.setBackground(kNotExecutedBg);
        } else {
            const bool isExecuted = lineNumber <= m_executedLineCount;
            sel.format.setBackground(isExecuted ? kExecutedBg : kNotExecutedBg);
            sel.format.setForeground(isExecuted ? kExecutedFg : kNotExecutedFg);
        }
        sel.format.setProperty(QTextFormat::FullWidthSelection, true);
        selections << sel;

        if (lineNumber == m_errorLine) {
            // Ligne fautive elle-même : surlignage de fond rouge distinct.
            // Celle-ci reste une sélection séparée (superposée), car ici on
            // veut spécifiquement AJOUTER un surlignage de fond par-dessus
            // le fond normal, ce qui fonctionne de façon fiable avec Qt --
            // seule la superposition pour la couleur du TEXTE posait
            // problème ci-dessus.
            QTextEdit::ExtraSelection errSel;
            errSel.cursor = sel.cursor;
            errSel.format.setBackground(kErrorLineBg);
            errSel.format.setForeground(QColor(150, 0, 0));
            errSel.format.setProperty(QTextFormat::FullWidthSelection, true);
            selections << errSel;
        }
    }

    setExtraSelections(selections);
}

QString CodeEditor::remainingText() const
{
    if (m_executedLineCount <= 0)
        return toPlainText();

    QTextBlock block = document()->findBlockByNumber(m_executedLineCount);
    if (!block.isValid())
        return QString();

    QTextCursor range(block);
    range.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
    // Etend la sélection jusqu'à la toute fin du document.
    QTextCursor endCursor(document());
    endCursor.movePosition(QTextCursor::End);
    range.setPosition(endCursor.position(), QTextCursor::KeepAnchor);

    // QTextCursor::selectedText() encode les retours à la ligne avec
    // U+2029 (paragraph separator) : on les reconvertit en '\n'.
    return range.selectedText().replace(QChar(0x2029), QChar('\n'));
}

int CodeEditor::insertAlreadyExecutedCode(const QString& text)
{
    QString toInsert = text;
    if (!toInsert.endsWith(QLatin1Char('\n')))
        toInsert += QLatin1Char('\n');

    QTextBlock insertionBlock = document()->findBlockByNumber(m_executedLineCount);
    QTextCursor c(document());
    if (insertionBlock.isValid()) {
        c.setPosition(insertionBlock.position());
    } else {
        c.movePosition(QTextCursor::End);
        if (!toPlainText().endsWith(QLatin1Char('\n')) && !toPlainText().isEmpty())
            toInsert.prepend(QLatin1Char('\n'));
    }

    const int linesInserted = toInsert.count(QLatin1Char('\n'));
    c.insertText(toInsert);

    setExecutedLineCount(m_executedLineCount + linesInserted);
    return linesInserted;
}

void CodeEditor::ensureEditableTail()
{
    // Si toutes les lignes actuelles du document sont marquées comme
    // exécutées, il n'existe plus AUCUNE position de curseur non protégée
    // (même la toute fin du document appartient encore, au sens de
    // cursorInProtectedZone(), au dernier bloc exécuté). On ajoute donc un
    // bloc vide en fin de document, qui lui n'est pas compté comme
    // exécuté : c'est là que l'utilisateur peut recommencer à taper.
    if (m_executedLineCount >= blockCount()) {
        QTextCursor c(document());
        c.movePosition(QTextCursor::End);
        c.insertBlock();
        applyHighlighting();
    }
}

void CodeEditor::ensureLineVisible(int line)
{
    if (line <= 0)
        return;
    QTextBlock block = document()->findBlockByNumber(line - 1);
    if (!block.isValid())
        return;
    setTextCursor(QTextCursor(block));
    ensureCursorVisible();
}

bool CodeEditor::cursorInProtectedZone() const
{
    if (m_executing)
        return true;
    return textCursor().blockNumber() + 1 <= m_executedLineCount;
}

void CodeEditor::mousePressEvent(QMouseEvent* event)
{
    QPlainTextEdit::mousePressEvent(event);
}

void CodeEditor::handleGutterClick(int y)
{
    // Ne permet de poser/enlever un point d'arrêt que hors exécution, OU
    // pendant une pause en cours de débogage (m_paused) -- contrairement à
    // l'édition générale du texte, qui reste bloquée dans ce second cas --
    // et uniquement sur une ligne de code non encore exécuté.
    if (m_executing && !m_paused)
        return;

    QTextCursor c = cursorForPosition(QPoint(0, y));
    const int lineNumber = c.blockNumber() + 1;
    if (lineNumber <= m_executedLineCount)
        return;

    // On peut toujours RETIRER un point d'arrêt déjà posé (par exemple un
    // point d'arrêt déplacé par édition sur une ligne devenue vide) ; en
    // revanche, on n'en pose un NOUVEAU que sur une ligne qui ressemble à
    // une instruction -- poser un point d'arrêt sur une ligne blanche, un
    // commentaire ou une déclaration def/class n'a pas de sens.
    if (!m_breakpoints.contains(lineNumber) && !isPlausibleBreakpointLine(lineNumber))
        return;

    emit breakpointToggleRequested(lineNumber);
}

bool CodeEditor::isPlausibleBreakpointLine(int lineNumber) const
{
    QTextBlock block = document()->findBlockByNumber(lineNumber - 1);
    if (!block.isValid())
        return false;

    const QString trimmed = block.text().trimmed();
    if (trimmed.isEmpty())
        return false; // ligne blanche

    if (trimmed.startsWith(QLatin1Char('#')))
        return false; // commentaire pur

    static const QRegularExpression declRe(QStringLiteral("^(def|class)\\b.*:\\s*$"));
    if (declRe.match(trimmed).hasMatch())
        return false; // déclaration "def ...:" / "class ...:" -- ne
                       // s'exécute qu'une fois, à la définition, jamais à
                       // chaque appel : s'y arrêter n'apporte rien en
                       // pratique

    return true;
}

void CodeEditor::keyPressEvent(QKeyEvent* event)
{
    // Ctrl+Tab : complétion façon readline
    if (event->key() == Qt::Key_Tab && (event->modifiers() & Qt::ControlModifier)) {
        QTextCursor c = textCursor();
        const int posInBlock = c.positionInBlock();
        const QString lineText = c.block().text().left(posInBlock);
        emit completionRequested(lineText);
        return;
    }

    // Toute touche qui n'est PAS Shift+Up/Down met fin à un cycle
    // d'historique en cours (le texte actuellement inséré devient du
    // texte normal) -- filet de sécurité en plus de keyReleaseEvent()
    // (relâchement de Shift), pour les cas où le focus ou la touche
    // change sans qu'un keyReleaseEvent(Shift) propre ne soit reçu.
    const bool isShiftUp = event->key() == Qt::Key_Up
            && (event->modifiers() & Qt::ShiftModifier)
            && !(event->modifiers() & Qt::ControlModifier);
    const bool isShiftDown = event->key() == Qt::Key_Down
            && (event->modifiers() & Qt::ShiftModifier)
            && !(event->modifiers() & Qt::ControlModifier);
    if (m_historyCyclingActive && !isShiftUp && !isShiftDown) {
        endHistoryCycle();
    }

    // Shift+Up : insère la dernière instruction exécutée avec succès à la
    // position du curseur (nouveau cycle), ou recule d'un cran dans
    // l'historique (plus ancien) si un cycle est déjà en cours. Shift+Down
    // avance vers le plus récent, jusqu'à ne rien insérer -- mais
    // uniquement si un cycle est DÉJÀ en cours : sans cycle actif,
    // Shift+Down garde son comportement standard (extension de sélection).
    if (isShiftUp) {
        if (m_executing) {
            return;
        }
        if (m_historyCyclingActive) {
            stepHistoryCycle(+1);
        } else if (!cursorInProtectedZone()) {
            emit historyRequested(); // PyConsoleWidget répond via beginHistoryCycle()
        }
        return;
    }
    if (isShiftDown && m_historyCyclingActive) {
        if (m_executing) {
            return;
        }
        stepHistoryCycle(-1);
        return;
    }

    if (m_completionPopup->isVisible()) {
        if (event->key() == Qt::Key_Escape) {
            m_completionPopup->hide();
            return;
        }
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            if (auto* item = m_completionPopup->currentItem()) {
                insertCompletion(item->text());
            }
            return;
        }
        if (event->key() == Qt::Key_Down || event->key() == Qt::Key_Up) {
            QCoreApplication::sendEvent(m_completionPopup, event);
            return;
        }
    }

    // Zone protégée : on n'autorise que la navigation et la copie.
    if (cursorInProtectedZone()) {
        const bool isNavigation =
            event->key() == Qt::Key_Left || event->key() == Qt::Key_Right ||
            event->key() == Qt::Key_Up || event->key() == Qt::Key_Down ||
            event->key() == Qt::Key_Home || event->key() == Qt::Key_End ||
            event->key() == Qt::Key_PageUp || event->key() == Qt::Key_PageDown ||
            (event->matches(QKeySequence::Copy));
        if (!isNavigation) {
            return; // ignore toute modification
        }
    }

    QPlainTextEdit::keyPressEvent(event);
}

void CodeEditor::keyReleaseEvent(QKeyEvent* event)
{
    // Fin du cycle d'historique dès que Shift est relâchée : "tant que la
    // touche Shift est pressée" (cf. beginHistoryCycle()/stepHistoryCycle()).
    if (event->key() == Qt::Key_Shift && m_historyCyclingActive) {
        endHistoryCycle();
    }
    QPlainTextEdit::keyReleaseEvent(event);
}

void CodeEditor::insertFromMimeData(const QMimeData* source)
{
    if (cursorInProtectedZone())
        return;
    QPlainTextEdit::insertFromMimeData(source);
}

void CodeEditor::showCompletionPopup(const QStringList& items, const QString& prefix)
{
    m_completionPrefix = prefix;

    if (items.isEmpty()) {
        m_completionPopup->hide();
        return;
    }
    m_completionPopup->clear();
    m_completionPopup->addItems(items);
    m_completionPopup->setCurrentRow(0);

    const QRect r = cursorRect();
    const QPoint global = viewport()->mapToGlobal(r.bottomLeft());
    m_completionPopup->move(global);
    m_completionPopup->resize(260, qMin(200, 20 * items.size() + 10));
    m_completionPopup->show();
    m_completionPopup->setFocus();
}

void CodeEditor::insertCompletion(const QString& completion)
{
    // rlcompleter renvoie toujours le candidat COMPLET (préfixe déjà tapé
    // inclus, ex: "os.path" pour un préfixe "os.pa") -- on n'insère donc
    // que ce qui dépasse ce préfixe, à la position actuelle du curseur.
    // On n'utilise plus WordUnderCursor pour sélectionner/remplacer : cette
    // sélection Qt s'arrête aux limites de "mot" Unicode, qui ne couvrent
    // pas le point ('.') d'un identifiant qualifié -- pour "os.pa", elle
    // n'aurait sélectionné que "pa", et remplacer "pa" par le candidat
    // complet "os.path" aurait dupliqué le préfixe ("os.os.path").
    QTextCursor c = textCursor();
    if (completion.startsWith(m_completionPrefix) && completion.size() > m_completionPrefix.size()) {
        c.insertText(completion.mid(m_completionPrefix.size()));
    } else if (!completion.startsWith(m_completionPrefix)) {
        // Cas de repli (ne devrait pas arriver avec rlcompleter) : le
        // candidat ne commence pas par le préfixe attendu, on insère tout.
        c.insertText(completion);
    }
    setTextCursor(c);
    m_completionPopup->hide();
}

void CodeEditor::beginHistoryCycle(const QStringList& items)
{
    if (items.isEmpty() || m_executing || cursorInProtectedZone())
        return;

    m_historyItems = items; // la plus récente en tête (index 0)
    m_historyIndex = 0;

    QTextCursor c = textCursor();
    m_historyInsertStart = c.position();
    c.insertText(items.at(0));
    m_historyInsertEnd = c.position();
    setTextCursor(c);

    m_historyCyclingActive = true;
}

void CodeEditor::stepHistoryCycle(int direction)
{
    if (!m_historyCyclingActive)
        return;

    // -1 = "rien inséré" ; 0..N-1 = index dans m_historyItems (plus
    // ancien à mesure que l'index augmente). Bloqué aux deux extrémités :
    // au-delà de la plus ancienne (Shift+Up répété), ou en-deçà de "rien"
    // (Shift+Down répété).
    const int newIndex = qBound(-1, m_historyIndex + direction, m_historyItems.size() - 1);
    if (newIndex == m_historyIndex)
        return;
    m_historyIndex = newIndex;

    QTextCursor c = textCursor();
    c.setPosition(m_historyInsertStart);
    c.setPosition(m_historyInsertEnd, QTextCursor::KeepAnchor);
    const QString replacement = (m_historyIndex >= 0) ? m_historyItems.at(m_historyIndex) : QString();
    c.insertText(replacement);
    m_historyInsertEnd = m_historyInsertStart + replacement.size();
    setTextCursor(c);
}

void CodeEditor::endHistoryCycle()
{
    // Le texte actuellement inséré (ou son absence, si on est descendu
    // jusqu'à "rien") devient définitif : simple remise à zéro de l'état
    // de suivi, aucune modification du document nécessaire ici.
    m_historyCyclingActive = false;
    m_historyIndex = -1;
    m_historyItems.clear();
}
