#ifndef QT_PYTHON_SYNTAX_HIGHLIGHTER_H
#define QT_PYTHON_SYNTAX_HIGHLIGHTER_H

#include <QSyntaxHighlighter>
#ifndef QT_5
#	include <QRegularExpression>
#endif	// QT_5

/**
 * Classe permettant la mise en évidence des mots clés du langage python dans
 * un éditeur de texte type <I>QTextEdit</I>.
 *
 * @author		Charles PIGNEROL, CEA/DAM/DSSI
 */
class QtPythonSyntaxHighlighter : public QSyntaxHighlighter
{
	Q_OBJECT

	public :

	/**
	 * Constructeur. RAS.
	 */
	QtPythonSyntaxHighlighter (QTextDocument* parent);

	/**
	 * Destructeur. RAS.
	 */
	virtual ~QtPythonSyntaxHighlighter ( );


	protected :

	/**
	 * Met en évidence les mots clés contenus dans le texte transmis en
	 * argument.
	 */
	virtual void highlightBlock (const QString& text);


	private :

	/**
	 * Constructeur de copie et opérateur = : interdits.
	 */
	QtPythonSyntaxHighlighter (const QtPythonSyntaxHighlighter&);
	QtPythonSyntaxHighlighter& operator = (const QtPythonSyntaxHighlighter&);

	struct HighlightingRule
	{
#ifdef QT_5
		QRegExp				pattern;
#else   // QT_5
		QRegularExpression	pattern;
#endif  // QT_5

		QTextCharFormat	format;
	};	// struct HighlightingRule

	QVector<HighlightingRule>	_highlightingRules;
};	// class QtPythonSyntaxHighlighter


#endif	// QT_PYTHON_SYNTAX_HIGHLIGHTER_H
