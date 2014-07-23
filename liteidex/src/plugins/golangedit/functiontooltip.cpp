/**************************************************************************
** This file is part of LiteIDE
**
** Copyright (c) 2011-2014 LiteIDE Team. All rights reserved.
**
** This library is free software; you can redistribute it and/or
** modify it under the terms of the GNU Lesser General Public
** License as published by the Free Software Foundation; either
** version 2.1 of the License, or (at your option) any later version.
**
** This library is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
** Lesser General Public License for more details.
**
** In addition, as a special exception,  that plugins developed for LiteIDE,
** are allowed to remain closed sourced and can be distributed under any license .
** These rights are included in the file LGPL_EXCEPTION.txt in this package.
**
**************************************************************************/
// Module: functiontooltip.cpp
// Creator: visualfc <visualfc@gmail.com>

#include "functiontooltip.h"
#include "cplusplus/SimpleLexer.h"

#include <QDesktopWidget>
#include <QApplication>
#include <QStylePainter>
#include <QStyleOptionFrame>
#include <QToolButton>
#include <QHBoxLayout>
#include <QDebug>
//lite_memory_check_begin
#if defined(WIN32) && defined(_MSC_VER) &&  defined(_DEBUG)
     #define _CRTDBG_MAP_ALLOC
     #include <stdlib.h>
     #include <crtdbg.h>
     #define DEBUG_NEW new( _NORMAL_BLOCK, __FILE__, __LINE__ )
     #define new DEBUG_NEW
#endif
//lite_memory_check_end

using namespace CPlusPlus;

void FakeToolTipFrame::paintEvent(QPaintEvent *)
{
    QStylePainter p(this);
    QStyleOptionFrame opt;
    opt.init(this);
    p.drawPrimitive(QStyle::PE_PanelTipLabel, opt);
    p.end();
}

void FakeToolTipFrame::resizeEvent(QResizeEvent *)
{
    QStyleHintReturnMask frameMask;
    QStyleOption option;
    option.init(this);
    if (style()->styleHint(QStyle::SH_ToolTip_Mask, &option, this, &frameMask))
        setMask(frameMask.region);
}

FunctionTooltip::FunctionTooltip(int maxTipCount, QObject *parent)
    : QObject(parent), m_popup(0),m_editor(0), m_lexer(0), m_maxTipCount(maxTipCount)
{
    qApp->installEventFilter(this);
}

FunctionTooltip::~FunctionTooltip()
{
    delete m_popup;
}

void FunctionTooltip::showFunctionHint(LiteApi::ITextEditor *editor, LiteApi::ITextLexer *lexer, int startPosition, const QString &tip)
{
    if (!editor) {
        return;
    }
    if (!m_popup) {
        m_popup = new FakeToolTipFrame(editor->widget());
        QHBoxLayout *hbox = new QHBoxLayout;
        hbox->setContentsMargins(4,1,4,1);
        hbox->setSpacing(0);
        m_label = new QLabel;
        hbox->addWidget(m_label);
        m_popup->setLayout(hbox);
    }
    if (m_editor != editor) {
        m_popup->setParent(editor->widget());
    }
    m_editor = editor;
    m_lexer = lexer;
    m_currentarg = -1;

    m_tip = tip;
    m_escapePressed = false;
    m_startpos = startPosition;

    saveTip(startPosition,tip);

    updateArgumentHighlight();

    showPopup(m_startpos);
}

bool FunctionTooltip::eventFilter(QObject *obj, QEvent *e)
{
    if (!m_editor ) {
        return false;
    }
    switch (e->type()) {
    case QEvent::ShortcutOverride:
        if (static_cast<QKeyEvent*>(e)->key() == Qt::Key_Escape) {
            m_escapePressed = true;
        }
        break;
    case QEvent::KeyPress:
        if (static_cast<QKeyEvent*>(e)->key() == Qt::Key_Escape) {
            m_escapePressed = true;
        }
        break;
    case QEvent::KeyRelease: {
            if (static_cast<QKeyEvent*>(e)->key() == Qt::Key_Escape && m_escapePressed) {
                hide();
                return false;
            }
            QWidget *widget = qobject_cast<QWidget *>(obj);
            if (! (m_editor->widget()->isAncestorOf(widget))) {
                return false;
            }
            if (static_cast<QKeyEvent*>(e)->key() == Qt::Key_Comma) {
                int pos = m_lexer->startOfFunctionCall(m_editor->textCursor());
                if (pos != -1 && pos+1 != m_startpos) {
                    m_startpos = pos+1;
                    if (restoreTip(m_startpos)) {
                        updateArgumentHighlight();
                        showPopup(m_startpos);
                    } else {
                        hide();
                    }
                    return false;
                }
            }
            if (m_popup->isVisible()) {
                updateArgumentHighlight();
            }
        }
        break;
    case QEvent::WindowDeactivate:
    case QEvent::FocusOut:
    case QEvent::Resize:
    case QEvent::Move:
        if (obj != m_editor->widget())
            break;
        hide();
        break;
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonRelease:
    case QEvent::MouseButtonDblClick:
    case QEvent::Wheel: {
            QWidget *widget = qobject_cast<QWidget *>(obj);
            if (! (widget == m_label || m_popup->isAncestorOf(widget))) {
                hide();
            }
        }
        break;
    default:
        break;
    }
    return false;
}

void FunctionTooltip::updateArgumentHighlight()
{
    int curpos = m_editor->position();
    if (curpos < m_startpos) {
        hide();
        return;
    }

    QString str = m_editor->textAt(m_startpos, curpos - m_startpos);
    int argnr = 0;
    int parcount = 0;

    LanguageFeatures features;
    features.golangEnable = true;

    SimpleLexer tokenize;
    tokenize.setLanguageFeatures(features);
    QList<Token> tokens = tokenize(str);
    for (int i = 0; i < tokens.count(); ++i) {
        const Token &tk = tokens.at(i);
        if (tk.is(T_LPAREN))
            ++parcount;
        else if (tk.is(T_RPAREN))
            --parcount;
        else if (! parcount && tk.is(T_COMMA))
            ++argnr;
    }

    if (m_currentarg != argnr) {
        m_currentarg = argnr;
        updateHintText();
    }

    if (parcount < 0)
        hide();
}

void FunctionTooltip::updateHintText()
{
    QString hintText;
    hintText = m_tip;
    /*
    for (int i = 0; i < m_args.size(); i++) {
        if (i != 0) {
            hintText += ",";
        }
        if (i == m_currentarg) {
            hintText += "<b>"+Qt::escape(m_args[i])+"</b>";
        } else {
            hintText += Qt::escape(m_args[i]);
        }
    }
    */
    m_label->setText(hintText);
}

void FunctionTooltip::showPopup(int startpos)
{
    m_popup->setFixedWidth(m_popup->minimumSizeHint().width());

    const QDesktopWidget *desktop = QApplication::desktop();
#ifdef Q_WS_MAC
    const QRect screen = desktop->availableGeometry(desktop->screenNumber(m_editor->widget()));
#else
    const QRect screen = desktop->screenGeometry(desktop->screenNumber(m_editor->widget()));
#endif

    const QSize sz = m_popup->sizeHint();
    QPoint pos = m_editor->cursorRect(startpos).topLeft();
    pos.setY(pos.y() - sz.height() - 1);

    if (pos.x() + sz.width() > screen.right())
        pos.setX(screen.right() - sz.width());

    m_popup->move(pos);
    if (!m_popup->isVisible()) {
        m_popup->show();
    }
}

void FunctionTooltip::hide()
{
    if (!m_popup->isVisible()) {
        return;
    }
    m_popup->hide();
    m_startpos = -1;
}

void FunctionTooltip::saveTip(int startPos, const QString &text)
{
    QList<TipInfo> &infoList = m_infoMap[m_editor];
    QMutableListIterator<TipInfo> it(infoList);
    while(it.hasNext()) {
        TipInfo &info = it.next();
        if (info.startPos == startPos) {
            info.tip = text;
            return;
        }
    }
    infoList.append(TipInfo(startPos,text));
    if (infoList.size() >= m_maxTipCount) {
        infoList.removeFirst();
    }
}

bool FunctionTooltip::restoreTip(int startpos)
{
    QList<TipInfo> &infoList = m_infoMap[m_editor];
    foreach(TipInfo info, infoList) {
        if (info.startPos == startpos) {
            m_tip = info.tip;
            return true;
        }
    }
    return false;
}
