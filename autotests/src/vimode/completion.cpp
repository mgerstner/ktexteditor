/*
 * This file is part of the KDE libraries
 *
 * Copyright (C) 2014 Miquel Sabaté Solà <mikisabate@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */


#include <katecompletionwidget.h>
#include <kateconfig.h>
#include <kateglobal.h>
#include <vimode/emulatedcommandbar/emulatedcommandbar.h>
#include <katewordcompletion.h>
#include "completion.h"
#include "fakecodecompletiontestmodel.h"
#include "vimode/mappings.h"
#include "vimode/globalstate.h"

using namespace KTextEditor;
using KateVi::Mappings;


QTEST_MAIN(CompletionTest)

//BEGIN: VimCodeCompletionTestModel

VimCodeCompletionTestModel::VimCodeCompletionTestModel(KTextEditor::View *parent)
    : KTextEditor::CodeCompletionModel(parent)
{
    setRowCount(3);
    cc()->setAutomaticInvocationEnabled(true);
    // It would add additional items and we don't want that in tests
    cc()->unregisterCompletionModel(KTextEditor::EditorPrivate::self()->wordCompletionModel());
    cc()->registerCompletionModel(this);
}

QVariant VimCodeCompletionTestModel::data(const QModelIndex &index, int role) const
{
    // Order is important, here, as the completion widget seems to do its own sorting.
    const char *completions[] = { "completion1", "completion2", "completion3" };
    if (role == Qt::DisplayRole) {
        if (index.column() == Name) {
            return QString(completions[index.row()]);
        }
    }
    return QVariant();
}

CodeCompletionInterface * VimCodeCompletionTestModel::cc() const
{
    return dynamic_cast<CodeCompletionInterface *>(const_cast<QObject *>(QObject::parent()));
}

//END: VimCodeCompletionTestModel

//BEGIN: CodeCompletionInterface

FailTestOnInvocationModel::FailTestOnInvocationModel(KTextEditor::View *parent)
    : KTextEditor::CodeCompletionModel(parent)
{
    setRowCount(3);
    cc()->setAutomaticInvocationEnabled(true);
    // It would add additional items and we don't want that in tests.
    cc()->unregisterCompletionModel(EditorPrivate::self()->wordCompletionModel());
    cc()->registerCompletionModel(this);
}

QVariant FailTestOnInvocationModel::data(const QModelIndex &index, int role) const
{
    Q_UNUSED(index);
    Q_UNUSED(role);

    failTest();
    return QVariant();
}

void FailTestOnInvocationModel::failTest() const
{
    QFAIL("Shouldn't be invoking me!");
}

CodeCompletionInterface * FailTestOnInvocationModel::cc() const
{
    return dynamic_cast<CodeCompletionInterface *>(const_cast<QObject *>(QObject::parent()));
}

//END: CodeCompletionInterface

//BEGIN: CompletionTest

void CompletionTest::FakeCodeCompletionTests()
{
    // Test that FakeCodeCompletionTestModel behaves similar to the code-completion in e.g. KDevelop.
    const bool oldStealKeys = KateViewConfig::global()->viInputModeStealKeys();
    KateViewConfig::global()->setViInputModeStealKeys(true); // For Ctrl-P, Ctrl-N etc
    ensureKateViewVisible(); // KTextEditor::ViewPrivate needs to be visible for the completion widget.
    FakeCodeCompletionTestModel *fakeCodeCompletionModel = new FakeCodeCompletionTestModel(kate_view);
    kate_view->registerCompletionModel(fakeCodeCompletionModel);
    fakeCodeCompletionModel->setCompletions({ "completionA", "completionB", "completionC" });
    DoTest("", "i\\ctrl-p\\enter", "completionC");
    DoTest("", "i\\ctrl-p\\ctrl-p\\enter", "completionB");
    DoTest("", "i\\ctrl-p\\ctrl-p\\ctrl-p\\enter", "completionA");
    DoTest("", "i\\ctrl-p\\ctrl-p\\ctrl-p\\ctrl-p\\enter", "completionC");
    // If no word before cursor, don't delete any text.
    BeginTest("");
    clearTrackedDocumentChanges();
    TestPressKey("i\\ctrl- \\enter");
    QCOMPARE(m_docChanges.length(), 1);
    FinishTest("completionA");
    // Apparently, we must delete the word before the cursor upon completion
    // (even if we replace it with identical text!)
    BeginTest("compl");
    TestPressKey("ea");
    clearTrackedDocumentChanges();
    TestPressKey("\\ctrl- \\enter");
    QCOMPARE(m_docChanges.size(), 2);
    QCOMPARE(m_docChanges[0].changeType(), DocChange::TextRemoved);
    QCOMPARE(m_docChanges[0].changeRange(), Range(Cursor(0, 0), Cursor(0, 5)));
    QCOMPARE(m_docChanges[1].changeType(), DocChange::TextInserted);
    QCOMPARE(m_docChanges[1].changeRange(), Range(Cursor(0, 0), Cursor(0, 11)));
    QCOMPARE(m_docChanges[1].newText(), QString("completionA"));
    FinishTest("completionA");
    // A "word" is currently alphanumeric, plus underscore.
    fakeCodeCompletionModel->setCompletions({ "w_123completion" });
    BeginTest("(w_123");
    TestPressKey("ea");
    clearTrackedDocumentChanges();
    TestPressKey("\\ctrl- \\enter");
    QCOMPARE(m_docChanges.size(), 2);
    QCOMPARE(m_docChanges[0].changeType(), DocChange::TextRemoved);
    QCOMPARE(m_docChanges[0].changeRange(), Range(Cursor(0, 1), Cursor(0, 6)));
    QCOMPARE(m_docChanges[1].changeType(), DocChange::TextInserted);
    QCOMPARE(m_docChanges[1].changeRange(), Range(Cursor(0, 1), Cursor(0, 16)));
    QCOMPARE(m_docChanges[1].newText(), QString("w_123completion"));
    FinishTest("(w_123completion");
    // "Removing tail on complete" is apparently done in three stages:
    // delete word up to the cursor; insert new word; then delete remainder.
    fakeCodeCompletionModel->setRemoveTailOnComplete(true);
    BeginTest("(w_123comp");
    TestPressKey("6li");
    clearTrackedDocumentChanges();
    TestPressKey("\\ctrl- \\enter");
    QCOMPARE(m_docChanges.size(), 3);
    QCOMPARE(m_docChanges[0].changeType(), DocChange::TextRemoved);
    QCOMPARE(m_docChanges[0].changeRange(), Range(Cursor(0, 1), Cursor(0, 6)));
    QCOMPARE(m_docChanges[1].changeType(), DocChange::TextInserted);
    QCOMPARE(m_docChanges[1].changeRange(), Range(Cursor(0, 1), Cursor(0, 16)));
    QCOMPARE(m_docChanges[1].newText(), QString("w_123completion"));
    QCOMPARE(m_docChanges[2].changeType(), DocChange::TextRemoved);
    QCOMPARE(m_docChanges[2].changeRange(), Range(Cursor(0, 16), Cursor(0, 20)));
    FinishTest("(w_123completion");

    // If we don't remove tail, just delete up to the cursor and insert.
    fakeCodeCompletionModel->setRemoveTailOnComplete(false);
    BeginTest("(w_123comp");
    TestPressKey("6li");
    clearTrackedDocumentChanges();
    TestPressKey("\\ctrl- \\enter");
    QCOMPARE(m_docChanges.size(), 2);
    QCOMPARE(m_docChanges[0].changeType(), DocChange::TextRemoved);
    QCOMPARE(m_docChanges[0].changeRange(), Range(Cursor(0, 1), Cursor(0, 6)));
    QCOMPARE(m_docChanges[1].changeType(), DocChange::TextInserted);
    QCOMPARE(m_docChanges[1].changeRange(), Range(Cursor(0, 1), Cursor(0, 16)));
    QCOMPARE(m_docChanges[1].newText(), QString("w_123completion"));
    FinishTest("(w_123completioncomp");

    // If no opening bracket after the cursor, a function taking no arguments
    // is added as "function()", and the cursor placed after the closing ")".
    // The addition of "function()" is done in two steps: first "function", then "()".
    BeginTest("object->");
    fakeCodeCompletionModel->setCompletions({ "functionCall()" });
    fakeCodeCompletionModel->setRemoveTailOnComplete(true);
    clearTrackedDocumentChanges();
    TestPressKey("$a\\ctrl- \\enter");
    QCOMPARE(m_docChanges.size(), 2);
    QCOMPARE(m_docChanges[0].changeType(), DocChange::TextInserted);
    QCOMPARE(m_docChanges[0].changeRange(), Range(Cursor(0, 8), Cursor(0, 20)));
    QCOMPARE(m_docChanges[0].newText(), QString("functionCall"));
    QCOMPARE(m_docChanges[1].changeRange(), Range(Cursor(0, 20), Cursor(0, 22)));
    QCOMPARE(m_docChanges[1].newText(), QString("()"));
    TestPressKey("X");
    FinishTest("object->functionCall()X");

    // If no opening bracket after the cursor, a function taking at least one argument
    // is added as "function()", and the cursor placed after the opening "(".
    // The addition of "function()" is done in two steps: first "function", then "()".
    qDebug() << "Fleep";
    BeginTest("object->");
    fakeCodeCompletionModel->setCompletions({ "functionCall(...)" });
    fakeCodeCompletionModel->setRemoveTailOnComplete(true);
    clearTrackedDocumentChanges();
    TestPressKey("$a\\ctrl- \\enter");
    QCOMPARE(m_docChanges.size(), 2);
    QCOMPARE(m_docChanges[0].changeType(), DocChange::TextInserted);
    QCOMPARE(m_docChanges[0].changeRange(), Range(Cursor(0, 8), Cursor(0, 20)));
    QCOMPARE(m_docChanges[0].newText(), QString("functionCall"));
    QCOMPARE(m_docChanges[1].changeType(), DocChange::TextInserted);
    QCOMPARE(m_docChanges[1].changeRange(), Range(Cursor(0, 20), Cursor(0, 22)));
    QCOMPARE(m_docChanges[1].newText(), QString("()"));
    TestPressKey("X");
    FinishTest("object->functionCall(X)");

    // If there is an opening bracket after the cursor, we merge the function call
    // with that.
    // Even if the function takes no arguments, we still place the cursor after the opening bracket,
    // in contrast to the case where there is no opening bracket after the cursor.
    // No brackets are added.  No removals occur if there is no word before the cursor.
    BeginTest("object->(");
    fakeCodeCompletionModel->setCompletions({ "functionCall()" });
    fakeCodeCompletionModel->setRemoveTailOnComplete(true);
    clearTrackedDocumentChanges();
    TestPressKey("f(i\\ctrl- \\enter");
    QCOMPARE(m_docChanges.size(), 1);
    QCOMPARE(m_docChanges[0].changeType(), DocChange::TextInserted);
    QCOMPARE(m_docChanges[0].changeRange(), Range(Cursor(0, 8), Cursor(0, 20)));
    QCOMPARE(m_docChanges[0].newText(), QString("functionCall"));
    TestPressKey("X");
    FinishTest("object->functionCall(X");

    // There can't be any non-whitespace between cursor position and opening bracket, though!
    BeginTest("object->|(   (");
    fakeCodeCompletionModel->setCompletions({ "functionCall()" });
    fakeCodeCompletionModel->setRemoveTailOnComplete(true);
    clearTrackedDocumentChanges();
    TestPressKey("f>a\\ctrl- \\enter");
    QCOMPARE(m_docChanges.size(), 2);
    QCOMPARE(m_docChanges[0].changeType(), DocChange::TextInserted);
    QCOMPARE(m_docChanges[0].changeRange(), Range(Cursor(0, 8), Cursor(0, 20)));
    QCOMPARE(m_docChanges[0].newText(), QString("functionCall"));
    QCOMPARE(m_docChanges[1].changeRange(), Range(Cursor(0, 20), Cursor(0, 22)));
    QCOMPARE(m_docChanges[1].newText(), QString("()"));
    TestPressKey("X");
    FinishTest("object->functionCall()X|(   (");

    // Whitespace before the bracket is fine, though.
    BeginTest("object->    (<-Cursor here!");
    fakeCodeCompletionModel->setCompletions({ "functionCall()" });
    fakeCodeCompletionModel->setRemoveTailOnComplete(true);
    clearTrackedDocumentChanges();
    TestPressKey("f>a\\ctrl- \\enter");
    QCOMPARE(m_docChanges.size(), 1);
    QCOMPARE(m_docChanges[0].changeType(), DocChange::TextInserted);
    QCOMPARE(m_docChanges[0].changeRange(), Range(Cursor(0, 8), Cursor(0, 20)));
    QCOMPARE(m_docChanges[0].newText(), QString("functionCall"));
    TestPressKey("X");
    FinishTest("object->functionCall    (X<-Cursor here!");

    // Be careful with positioning the cursor if we delete leading text!
    BeginTest("object->    (<-Cursor here!");
    fakeCodeCompletionModel->setCompletions({ "functionCall()" });
    fakeCodeCompletionModel->setRemoveTailOnComplete(true);
    clearTrackedDocumentChanges();
    TestPressKey("f>afunct");
    clearTrackedDocumentChanges();
    TestPressKey("\\ctrl- \\enter");
    QCOMPARE(m_docChanges.size(), 2);
    QCOMPARE(m_docChanges[0].changeType(), DocChange::TextRemoved);
    QCOMPARE(m_docChanges[0].changeRange(), Range(Cursor(0, 8), Cursor(0, 13)));
    QCOMPARE(m_docChanges[1].changeType(), DocChange::TextInserted);
    QCOMPARE(m_docChanges[1].changeRange(), Range(Cursor(0, 8), Cursor(0, 20)));
    QCOMPARE(m_docChanges[1].newText(), QString("functionCall"));
    TestPressKey("X");
    FinishTest("object->functionCall    (X<-Cursor here!");

    // If we're removing tail on complete, it's whether there is a suitable opening
    // bracket *after* the word (not the cursor) that's important.
    BeginTest("object->function    (<-Cursor here!");
    fakeCodeCompletionModel->setCompletions({ "functionCall()" });
    fakeCodeCompletionModel->setRemoveTailOnComplete(true);
    clearTrackedDocumentChanges();
    TestPressKey("12li"); // Start inserting before the "t" in "function"
    clearTrackedDocumentChanges();
    TestPressKey("\\ctrl- \\enter");
    QCOMPARE(m_docChanges.size(), 3);
    QCOMPARE(m_docChanges[0].changeType(), DocChange::TextRemoved);
    QCOMPARE(m_docChanges[0].changeRange(), Range(Cursor(0, 8), Cursor(0, 12)));
    QCOMPARE(m_docChanges[1].changeType(), DocChange::TextInserted);
    QCOMPARE(m_docChanges[1].changeRange(), Range(Cursor(0, 8), Cursor(0, 20)));
    QCOMPARE(m_docChanges[1].newText(), QString("functionCall"));
    QCOMPARE(m_docChanges[2].changeType(), DocChange::TextRemoved);
    qDebug() << "m_docChanges[2].changeRange(): " << m_docChanges[2].changeRange();
    QCOMPARE(m_docChanges[2].changeRange(), Range(Cursor(0, 20), Cursor(0, 24)));
    TestPressKey("X");
    FinishTest("object->functionCall    (X<-Cursor here!");

    // Repeat of bracket-merging stuff, this time for functions that take at least one argument.
    BeginTest("object->(");
    fakeCodeCompletionModel->setCompletions({ "functionCall(...)" });
    fakeCodeCompletionModel->setRemoveTailOnComplete(true);
    clearTrackedDocumentChanges();
    TestPressKey("f(i\\ctrl- \\enter");
    QCOMPARE(m_docChanges.size(), 1);
    QCOMPARE(m_docChanges[0].changeType(), DocChange::TextInserted);
    qDebug() << "Range: " << m_docChanges[0].changeRange();
    QCOMPARE(m_docChanges[0].changeRange(), Range(Cursor(0, 8), Cursor(0, 20)));
    QCOMPARE(m_docChanges[0].newText(), QString("functionCall"));
    TestPressKey("X");
    FinishTest("object->functionCall(X");

    // There can't be any non-whitespace between cursor position and opening bracket, though!
    BeginTest("object->|(   (");
    fakeCodeCompletionModel->setCompletions({ "functionCall(...)" });
    fakeCodeCompletionModel->setRemoveTailOnComplete(true);
    clearTrackedDocumentChanges();
    TestPressKey("f>a\\ctrl- \\enter");
    QCOMPARE(m_docChanges.size(), 2);
    QCOMPARE(m_docChanges[0].changeType(), DocChange::TextInserted);
    QCOMPARE(m_docChanges[0].changeRange(), Range(Cursor(0, 8), Cursor(0, 20)));
    QCOMPARE(m_docChanges[0].newText(), QString("functionCall"));
    QCOMPARE(m_docChanges[1].changeRange(), Range(Cursor(0, 20), Cursor(0, 22)));
    QCOMPARE(m_docChanges[1].newText(), QString("()"));
    TestPressKey("X");
    FinishTest("object->functionCall(X)|(   (");

    // Whitespace before the bracket is fine, though.
    BeginTest("object->    (<-Cursor here!");
    qDebug() << "NooooO";
    fakeCodeCompletionModel->setCompletions({ "functionCall(...)" });
    fakeCodeCompletionModel->setRemoveTailOnComplete(true);
    clearTrackedDocumentChanges();
    TestPressKey("f>a\\ctrl- \\enter");
    QCOMPARE(m_docChanges.size(), 1);
    QCOMPARE(m_docChanges[0].changeType(), DocChange::TextInserted);
    QCOMPARE(m_docChanges[0].changeRange(), Range(Cursor(0, 8), Cursor(0, 20)));
    QCOMPARE(m_docChanges[0].newText(), QString("functionCall"));
    TestPressKey("X");
    FinishTest("object->functionCall    (X<-Cursor here!");

    // Be careful with positioning the cursor if we delete leading text!
    BeginTest("object->    (<-Cursor here!");
    fakeCodeCompletionModel->setCompletions({ "functionCall(...)" });
    fakeCodeCompletionModel->setRemoveTailOnComplete(true);
    clearTrackedDocumentChanges();
    TestPressKey("f>afunct");
    clearTrackedDocumentChanges();
    TestPressKey("\\ctrl- \\enter");
    QCOMPARE(m_docChanges.size(), 2);
    QCOMPARE(m_docChanges[0].changeType(), DocChange::TextRemoved);
    QCOMPARE(m_docChanges[0].changeRange(), Range(Cursor(0, 8), Cursor(0, 13)));
    QCOMPARE(m_docChanges[1].changeType(), DocChange::TextInserted);
    QCOMPARE(m_docChanges[1].changeRange(), Range(Cursor(0, 8), Cursor(0, 20)));
    QCOMPARE(m_docChanges[1].newText(), QString("functionCall"));
    TestPressKey("X");
    FinishTest("object->functionCall    (X<-Cursor here!");

    // If we're removing tail on complete, it's whether there is a suitable opening
    // bracket *after* the word (not the cursor) that's important.
    BeginTest("object->function    (<-Cursor here!");
    fakeCodeCompletionModel->setCompletions({ "functionCall(...)" });
    fakeCodeCompletionModel->setRemoveTailOnComplete(true);
    clearTrackedDocumentChanges();
    TestPressKey("12li"); // Start inserting before the "t" in "function"
    clearTrackedDocumentChanges();
    TestPressKey("\\ctrl- \\enter");
    QCOMPARE(m_docChanges.size(), 3);
    QCOMPARE(m_docChanges[0].changeType(), DocChange::TextRemoved);
    QCOMPARE(m_docChanges[0].changeRange(), Range(Cursor(0, 8), Cursor(0, 12)));
    QCOMPARE(m_docChanges[1].changeType(), DocChange::TextInserted);
    QCOMPARE(m_docChanges[1].changeRange(), Range(Cursor(0, 8), Cursor(0, 20)));
    QCOMPARE(m_docChanges[1].newText(), QString("functionCall"));
    QCOMPARE(m_docChanges[2].changeType(), DocChange::TextRemoved);
    qDebug() << "m_docChanges[2].changeRange(): " << m_docChanges[2].changeRange();
    QCOMPARE(m_docChanges[2].changeRange(), Range(Cursor(0, 20), Cursor(0, 24)));
    TestPressKey("X");
    FinishTest("object->functionCall    (X<-Cursor here!");

    // Deal with function completions which add a ";".
    BeginTest("");
    fakeCodeCompletionModel->setCompletions({ "functionCall();" });
    clearTrackedDocumentChanges();
    TestPressKey("ifun");
    clearTrackedDocumentChanges();
    TestPressKey("\\ctrl- \\enter");
    QCOMPARE(m_docChanges.size(), 3);
    QCOMPARE(m_docChanges[0].changeType(), DocChange::TextRemoved);
    QCOMPARE(m_docChanges[0].changeRange(), Range(Cursor(0, 0), Cursor(0, 3)));
    QCOMPARE(m_docChanges[1].changeType(), DocChange::TextInserted);
    QCOMPARE(m_docChanges[1].changeRange(), Range(Cursor(0, 0), Cursor(0, 12)));
    QCOMPARE(m_docChanges[1].newText(), QString("functionCall"));
    QCOMPARE(m_docChanges[2].changeType(), DocChange::TextInserted);
    QCOMPARE(m_docChanges[2].changeRange(), Range(Cursor(0, 12), Cursor(0, 15)));
    QCOMPARE(m_docChanges[2].newText(), QString("();"));
    FinishTest("functionCall();");

    BeginTest("");
    fakeCodeCompletionModel->setCompletions({ "functionCall();" });
    TestPressKey("ifun\\ctrl- \\enterX");
    FinishTest("functionCall();X");

    BeginTest("");
    fakeCodeCompletionModel->setCompletions({ "functionCall(...);" });
    clearTrackedDocumentChanges();
    TestPressKey("ifun");
    clearTrackedDocumentChanges();
    TestPressKey("\\ctrl- \\enter");
    QCOMPARE(m_docChanges.size(), 3);
    QCOMPARE(m_docChanges[0].changeType(), DocChange::TextRemoved);
    QCOMPARE(m_docChanges[0].changeRange(), Range(Cursor(0, 0), Cursor(0, 3)));
    QCOMPARE(m_docChanges[1].changeType(), DocChange::TextInserted);
    QCOMPARE(m_docChanges[1].changeRange(), Range(Cursor(0, 0), Cursor(0, 12)));
    QCOMPARE(m_docChanges[1].newText(), QString("functionCall"));
    QCOMPARE(m_docChanges[2].changeType(), DocChange::TextInserted);
    QCOMPARE(m_docChanges[2].changeRange(), Range(Cursor(0, 12), Cursor(0, 15)));
    QCOMPARE(m_docChanges[2].newText(), QString("();"));
    FinishTest("functionCall();");

    BeginTest("");
    fakeCodeCompletionModel->setCompletions({ "functionCall(...);" });
    TestPressKey("ifun\\ctrl- \\enterX");
    FinishTest("functionCall(X);");

    // Completions ending with ";" do not participate in bracket merging.
    BeginTest("(<-old bracket");
    fakeCodeCompletionModel->setCompletions({ "functionCall();" });
    TestPressKey("ifun\\ctrl- \\enterX");
    FinishTest("functionCall();X(<-old bracket");
    BeginTest("(<-old bracket");
    fakeCodeCompletionModel->setCompletions({ "functionCall(...);" });
    TestPressKey("ifun\\ctrl- \\enterX");
    FinishTest("functionCall(X);(<-old bracket");

    KateViewConfig::global()->setViInputModeStealKeys(oldStealKeys);
    kate_view->hide();
    mainWindow->hide();
    kate_view->unregisterCompletionModel(fakeCodeCompletionModel);
    delete fakeCodeCompletionModel;
}

void CompletionTest::CompletionTests()
{
    const bool oldRemoveTailOnCompletion = KateViewConfig::global()->wordCompletionRemoveTail();
    // For these tests, assume we don't swallow the tail on completion.
    KateViewConfig::global()->setWordCompletionRemoveTail(false);

    KateViewConfig::global()->setViInputModeStealKeys(true); // For Ctrl-P, Ctrl-N etc
    ensureKateViewVisible(); // KTextEditor::ViewPrivate needs to be visible for the completion widget.
    VimCodeCompletionTestModel *testModel = new VimCodeCompletionTestModel(kate_view);

    BeginTest("");
    TestPressKey("i\\ctrl-p");
    waitForCompletionWidgetToActivate();
    TestPressKey("\\return");
    FinishTest("completion3");

    BeginTest("");
    TestPressKey("i\\ctrl- ");
    waitForCompletionWidgetToActivate();
    TestPressKey("\\return");
    FinishTest("completion1");

    BeginTest("");
    TestPressKey("i\\ctrl-n");
    waitForCompletionWidgetToActivate();
    TestPressKey("\\return");
    FinishTest("completion1");

    // Test wraps around from top to bottom.
    BeginTest("");
    TestPressKey("i\\ctrl- \\ctrl-p");
    waitForCompletionWidgetToActivate();
    TestPressKey("\\return");
    FinishTest("completion3");

    // Test wraps around from bottom to top.
    BeginTest("");
    TestPressKey("i\\ctrl- \\ctrl-n\\ctrl-n\\ctrl-n");
    waitForCompletionWidgetToActivate();
    TestPressKey("\\return");
    FinishTest("completion1");

    // Test does not re-invoke completion when doing a "." repeat.
    BeginTest("");
    TestPressKey("i\\ctrl- ");
    waitForCompletionWidgetToActivate();
    TestPressKey("\\return\\ctrl-c");
    kate_view->unregisterCompletionModel(testModel);
    FailTestOnInvocationModel *failsTestOnInvocation = new FailTestOnInvocationModel(kate_view);
    TestPressKey("gg.");
    FinishTest("completion1completion1");
    kate_view->unregisterCompletionModel(failsTestOnInvocation);
    kate_view->registerCompletionModel(testModel);

    // Test that the full completion is repeated when repeat an insert that uses completion,
    // where the completion list was not manually invoked.
    BeginTest("");
    TestPressKey("i");
    // Simulate "automatic" invoking of completion.
    kate_view->completionWidget()->userInvokedCompletion();
    waitForCompletionWidgetToActivate();
    TestPressKey("\\return\\ctrl-cgg.");
    FinishTest("completion1completion1");

    clearAllMappings();
    // Make sure the "Enter"/ "Return" used when invoking completions is not swallowed before being
    // passed to the key mapper.
    kate_view->registerCompletionModel(testModel);
    vi_global->mappings()->add(Mappings::InsertModeMapping, "cb", "mapped-shouldntbehere", Mappings::Recursive);
    BeginTest("");
    TestPressKey("ic");
    kate_view->userInvokedCompletion();
    waitForCompletionWidgetToActivate();
    QVERIFY(kate_view->completionWidget()->isCompletionActive());
    TestPressKey("\\enterb");
    FinishTest("completion1b");
    BeginTest("");
    TestPressKey("ic");
    kate_view->userInvokedCompletion();
    waitForCompletionWidgetToActivate();
    QVERIFY(kate_view->completionWidget()->isCompletionActive());
    TestPressKey("\\returnb");
    FinishTest("completion1b");

    // Make sure the completion widget is dismissed on ESC, ctrl-c and ctrl-[.
    BeginTest("");
    TestPressKey("ic");
    kate_view->userInvokedCompletion();
    waitForCompletionWidgetToActivate();
    QVERIFY(kate_view->completionWidget()->isCompletionActive());
    TestPressKey("\\esc");
    QVERIFY(!kate_view->completionWidget()->isCompletionActive());
    FinishTest("c");
    BeginTest("");
    TestPressKey("ic");
    kate_view->userInvokedCompletion();
    waitForCompletionWidgetToActivate();
    QVERIFY(kate_view->completionWidget()->isCompletionActive());
    TestPressKey("\\ctrl-c");
    QVERIFY(!kate_view->completionWidget()->isCompletionActive());
    FinishTest("c");
    BeginTest("");
    TestPressKey("ic");
    kate_view->userInvokedCompletion();
    waitForCompletionWidgetToActivate();
    QVERIFY(kate_view->completionWidget()->isCompletionActive());
    TestPressKey("\\ctrl-[");
    QVERIFY(!kate_view->completionWidget()->isCompletionActive());
    FinishTest("c");
    kate_view->unregisterCompletionModel(testModel);

    // Check that the repeat-last-change handles Completions in the same way as Macros do
    // i.e. fairly intelligently :)
    FakeCodeCompletionTestModel *fakeCodeCompletionModel = new FakeCodeCompletionTestModel(kate_view);
    fakeCodeCompletionModel->setRemoveTailOnComplete(true);
    KateViewConfig::global()->setWordCompletionRemoveTail(true);
    kate_view->registerCompletionModel(fakeCodeCompletionModel);
    clearAllMacros();
    BeginTest("funct\nnoa\ncomtail\ncomtail");
    fakeCodeCompletionModel->setCompletions({ "completionA", "functionwithargs(...)", "noargfunction()" });
    fakeCodeCompletionModel->setFailTestOnInvocation(false);
    // Record 'a'.
    TestPressKey("i\\right\\right\\right\\right\\right\\ctrl- \\enterfirstArg"); // Function with args.
    TestPressKey("\\home\\down\\right\\right\\right\\ctrl- \\enter");            // Function no args.
    fakeCodeCompletionModel->setRemoveTailOnComplete(true);
    KateViewConfig::global()->setWordCompletionRemoveTail(true);
    TestPressKey("\\home\\down\\right\\right\\right\\ctrl- \\enter");   // Cut off tail.
    fakeCodeCompletionModel->setRemoveTailOnComplete(false);
    KateViewConfig::global()->setWordCompletionRemoveTail(false);
    TestPressKey("\\home\\down\\right\\right\\right\\ctrl- \\enter\\ctrl-c");   // Don't cut off tail.
    fakeCodeCompletionModel->setRemoveTailOnComplete(true);
    KateViewConfig::global()->setWordCompletionRemoveTail(true);
    // Replay.
    fakeCodeCompletionModel->setFailTestOnInvocation(true);
    kate_document->setText("funct\nnoa\ncomtail\ncomtail");
    TestPressKey("gg.");
    FinishTest("functionwithargs(firstArg)\nnoargfunction()\ncompletionA\ncompletionAtail");

    // Clear our log of completions for each change.
    BeginTest("");
    fakeCodeCompletionModel->setCompletions({ "completionA" });
    fakeCodeCompletionModel->setFailTestOnInvocation(false);
    TestPressKey("ciw\\ctrl- \\enter\\ctrl-c");
    fakeCodeCompletionModel->setCompletions({ "completionB" });
    TestPressKey("ciw\\ctrl- \\enter\\ctrl-c");
    fakeCodeCompletionModel->setFailTestOnInvocation(true);
    TestPressKey(".");
    FinishTest("completionB");

    kate_view->unregisterCompletionModel(fakeCodeCompletionModel);
    delete fakeCodeCompletionModel;
    KateViewConfig::global()->setWordCompletionRemoveTail(oldRemoveTailOnCompletion);

    // Hide the kate_view for subsequent tests.
    kate_view->hide();
    mainWindow->hide();
}

void CompletionTest::waitForCompletionWidgetToActivate()
{
    BaseTest::waitForCompletionWidgetToActivate(kate_view);
}

void CompletionTest::clearTrackedDocumentChanges()
{
    m_docChanges.clear();
}

//END: CompletionTest
