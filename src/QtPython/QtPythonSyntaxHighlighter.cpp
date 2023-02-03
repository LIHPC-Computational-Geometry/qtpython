#include "QtPython/QtPythonSyntaxHighlighter.h"

#include <iostream>
#include <assert.h>

using namespace std;


QtPythonSyntaxHighlighter::QtPythonSyntaxHighlighter (QTextDocument* parent)
	: QSyntaxHighlighter (parent)
{
	HighlightingRule	rule;
	QTextCharFormat		keywordFormat;
	QTextCharFormat		singleLineFormat;
	QTextCharFormat		quotationFormat;
	QTextCharFormat		functionFormat;
	keywordFormat.setForeground (Qt::darkBlue);
	keywordFormat.setFontWeight (QFont::Bold);
	QStringList	keywordPatterns;
	// ATTENTION : certains caractères spéciaux sont à double backslasher
	// (sinon risque de boucle infinie) : $, (,), *, +, ., ?, [, ,], ^, {, | et
	// }
	// cf. doc python reference/lexical_analysis.html :
	keywordPatterns << "\\band\\b" << "\\b\as\b" << "\\bassert\\b"
	                << "\\bbreak\\b" << "\\bclass\\b" << "\\bcontinue\\b"
	                << "\\bdef\\b" << "\\bdel\\b" << "\\belif\\b"
	                << "\\belse\\b" << "\\bexcept\\b" << "\\bexec\\b"
	                << "\\bfinally\\b" << "\\bfor\\b" << "\\bfrom\\b"
	                << "\\bglobal\\b" << "\\bif\\b" << "\\bimport\\b"
	                << "\\bin\\b" << "\\bis\\b" << "\\blambda\\b"
	                << "\\bnot\\b" << "\\bor\\b" << "\\bpass\\b"
	                << "\\bprint\\b" << "\\braise\\b" << "\\breturn\\b"
	                << "\\btry\\b" << "\\bwhile\\b" << "\\bwith\\b"
	                << "\\byield\\b" << "\\b_\\*\\b" << "\\b__\\*__\\b"
	                << "\\b__\\*\\b";
	foreach (const QString& pattern, keywordPatterns)
	{
		rule.pattern	= QRegExp (pattern);
		rule.format		= keywordFormat;
		_highlightingRules.append (rule);
	}	// foreach (const QString& pattern, keywordPatterns)

	quotationFormat.setForeground (Qt::red);
	rule.pattern	= QRegExp ("\".*\"");
	rule.format		= quotationFormat;
	_highlightingRules.append (rule);
	quotationFormat.setFontItalic (true);
	quotationFormat.setForeground (Qt::cyan);
	rule.pattern	= QRegExp ("\\b[A-Za-z0-9__]+(?=\\()");
	rule.format		= functionFormat;
	_highlightingRules.append (rule);
	singleLineFormat.setForeground (Qt::blue);
	rule.pattern	= QRegExp ("#[^\n]*");
	rule.format		= singleLineFormat;
	_highlightingRules.append (rule);
}	// QtPythonSyntaxHighlighter::QtPythonSyntaxHighlighter



QtPythonSyntaxHighlighter::QtPythonSyntaxHighlighter (
											const QtPythonSyntaxHighlighter&)
	: QSyntaxHighlighter ((QTextDocument*)0)
{
	assert (0 && "QtPythonSyntaxHighlighter copy constructor is not allowed.");
}	// QtPythonSyntaxHighlighter::QtPythonSyntaxHighlighter


QtPythonSyntaxHighlighter& QtPythonSyntaxHighlighter::operator = (
											const QtPythonSyntaxHighlighter&)
{
	assert (0 && "QtPythonSyntaxHighlighter copy constructor is not allowed.");
	return *this;
}	// QtPythonSyntaxHighlighter::operator =


QtPythonSyntaxHighlighter::~QtPythonSyntaxHighlighter ( )
{
}	// QtPythonSyntaxHighlighter::~QtPythonSyntaxHighlighter


void QtPythonSyntaxHighlighter::highlightBlock (const QString& text)
{
	foreach (const HighlightingRule& rule, _highlightingRules)
	{
		QRegExp	expression (rule.pattern);
		int		index	= expression.indexIn (text);
		while (index >= 0)
		{
			const int	length	= expression.matchedLength ( );
			setFormat (index, length, rule.format);
			index	= expression.indexIn (text, index + length);

			// Sécurité contre une expression mal formulée (=> boucle infinie) :
			if (length <= 0)
			{
				cerr << __FILE__ << ' ' << __LINE__
				     << " Erreur de formulation de l'expression régulière "
				     << expression.pattern ( ).toStdString ( ) << endl;
				break;
			}	// if (length <= 0)
		}	// while (index >= 0)
	}	// foreach (const HighlightingRule& rule, _highlightingRules)
}	// QtPythonSyntaxHighlighter::highlightBlock

