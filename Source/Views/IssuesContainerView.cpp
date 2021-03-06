/*
 * Copyright 2015 Your Name <your@email.address>
 * All rights reserved. Distributed under the terms of the MIT license.
 */

#include "IssuesContainerView.h"
#include "GithubClient.h"
#include "GithubIssue.h"
#include "GithubRepository.h"

#include "RepositoryTitleView.h"
#include "IssueListItem.h"
#include "Constants.h"
#include "MessageFinder.h"

#include <interface/GroupLayout.h>
#include <interface/LayoutBuilder.h>
#include <interface/Window.h>
#include <interface/ListView.h>
#include <interface/ListItem.h>
#include <interface/ScrollView.h>
#include <interface/Dragger.h>

#include <app/MessageRunner.h>
#include <app/Roster.h>


#include <posix/stdlib.h>
#include <posix/string.h>
#include <posix/stdio.h>

const float kDraggerSize = 7;
extern const char *kAppSignature;

IssuesContainerView::IssuesContainerView(BString repository, BString owner)
	:BView("Issues", B_DRAW_ON_CHILDREN)
	,fGithubClient(NULL)
	,fGithubRepository(NULL)
	,fRepositoryTitleView(NULL)
	,fListView(NULL)
	,fScrollView(NULL)
	,fDragger(NULL)
	,fAutoUpdateRunner(NULL)
	,fThreadId(-1)
	,fIsReplicant(false)
	,fRepository(repository)
	,fOwner(owner)
{
	SetupViews(fIsReplicant);
	SpawnDonwloadThread();
}

IssuesContainerView::IssuesContainerView(BMessage *message)
	:BView(message)
	,fGithubClient(NULL)
	,fGithubRepository(NULL)
	,fRepositoryTitleView(NULL)
	,fListView(NULL)
	,fScrollView(NULL)
	,fDragger(NULL)
	,fAutoUpdateRunner(NULL)
	,fThreadId(-1)
	,fIsReplicant(true)
{
	SetupViews(fIsReplicant);
	message->FindString("Repository", &fRepository);
	message->FindString("Owner", &fOwner);

	SpawnDonwloadThread();
}


IssuesContainerView::~IssuesContainerView()
{
	while(fListView->CountItems()) {
		delete fListView->RemoveItem(int32(0));
	}
	delete fListView;
	delete fGithubClient;
}

status_t
IssuesContainerView::Archive(BMessage* into, bool deep) const
{
	into->AddString("add_on", kAppSignature);
	into->AddString("Repository", fRepository);
	into->AddString("Owner", fOwner);
	return BView::Archive(into, false);
}

BArchivable*
IssuesContainerView::Instantiate(BMessage* archive)
{
	return new IssuesContainerView(archive);
}

status_t
IssuesContainerView::SaveState(BMessage* into, bool deep) const
{
	return B_OK;
}

void
IssuesContainerView::AttachedToWindow()
{
	StartAutoUpdater();
	ListView()->SetTarget(this);
	BView::AttachedToWindow();
}

void
IssuesContainerView::MessageReceived(BMessage *message)
{
	switch (message->what) {
		case kDataReceivedMessage: {
 			HandleParse(message);
			break;
		}

		case kIssueListInvokedMessage: {
			HandleListInvoke(message);
			break;
		}

		case kAutoUpdateMessage: {
			SpawnDonwloadThread();
			break;
		}
		default:
			BView::MessageReceived(message);
	}
}

void
IssuesContainerView::StartAutoUpdater()
{
	delete fAutoUpdateRunner;

	BMessenger view(this);
	bigtime_t seconds = 10;

	BMessage autoUpdateMessage(kAutoUpdateMessage);
	fAutoUpdateRunner = new BMessageRunner(view, &autoUpdateMessage, (bigtime_t) seconds * 1000 * 1000);
}

GithubClient*
IssuesContainerView::Client()
{
	if (fGithubClient == NULL) {
		fGithubClient = new GithubClient(this);
	}
	return fGithubClient;
}

BListView *
IssuesContainerView::ListView()
{
	if (fListView == NULL) {
		fListView = new BListView("Issues", B_SINGLE_SELECTION_LIST, B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE);
		fListView->SetInvocationMessage( new BMessage(kIssueListInvokedMessage ));
	}
	return fListView;
}

int32
IssuesContainerView::DownloadFunc(void *cookie)
{
	IssuesContainerView *view = static_cast<IssuesContainerView*>(cookie);
	view->RequestIssues();
	return 0;
}

void
IssuesContainerView::RequestIssues()
{
	if (fGithubRepository == NULL) {
		Client()->RequestRepository(fRepository.String(), fOwner.String());
	}
	Client()->RequestIssuesForRepository(fRepository.String(), fOwner.String());
}

void
IssuesContainerView::SpawnDonwloadThread()
{
	StopDownloadThread();

	fThreadId = spawn_thread(&DownloadFunc, "Download Issues", B_NORMAL_PRIORITY, this);
	if (fThreadId >= 0)
		resume_thread(fThreadId);
}

void
IssuesContainerView::StopDownloadThread()
{
	if (fThreadId == -1) {
		return;
	}
	wait_for_thread(fThreadId, NULL);
	fThreadId = -1;
}

void
IssuesContainerView::HandleListInvoke(BMessage *message)
{
	int32 index = B_ERROR;
	if (message->FindInt32("index", &index) == B_OK) {
		IssueListItem *listItem = dynamic_cast<IssueListItem*>(ListView()->ItemAt(index));

		if (listItem == NULL) {
			return;
		}

		const char *url = listItem->CurrentIssue().url.String();
		if (strlen(url) > 10 ) {
			be_roster->Launch("text/html", 1, const_cast<char **>(&url));
		}
	}
}

void
IssuesContainerView::AddIssues(BMessage *message)
{
	MessageFinder messageFinder;
	BMessage msg = messageFinder.FindMessage("nodes", *message);
	BMessage repositoriesMessage;

	char *name;
	uint32 type;
	int32 count;

	while(fListView->CountItems()) {
		delete fListView->RemoveItem(int32(0));
	}

	for (int32 i = 0; msg.GetInfo(B_MESSAGE_TYPE, i, &name, &type, &count) == B_OK; i++) {
		BMessage nodeMsg;
		if (msg.FindMessage(name, &nodeMsg) == B_OK) {
			GithubIssue issue(nodeMsg);
			IssueListItem *listItem = new IssueListItem(issue, fIsReplicant);
			fListView->AddItem( listItem );
		}
	}
}


void
IssuesContainerView::AddRepository(BMessage *message)
{
	MessageFinder messageFinder;
	BMessage msg = messageFinder.FindMessage("repository", *message);

	message->PrintToStream();

	delete fGithubRepository;
	fGithubRepository = new GithubRepository(msg);
	fRepositoryTitleView->SetRepository(fGithubRepository);
	printf("AddRepository \n");
}

void
IssuesContainerView::HandleParse(BMessage *message)
{
	if (fListView == NULL || fRepositoryTitleView == NULL) {
		return;
	}

	if (message->HasMessage("Issues")) {
		AddIssues(message);
	} else if (message->HasMessage("Repository")) {
		AddRepository(message);
	}

	float width;
	float height;
	fListView->GetPreferredSize(&width, &height);
	fListView->SetExplicitMinSize(BSize(320, height));

	if (fIsReplicant) {
		height += fRepositoryTitleView->MinSize().height;
		height += kDraggerSize;
		ResizeTo(Bounds().Width(), height);
	} else if (BWindow *window = dynamic_cast<BWindow*>(Parent())) {
		window->CenterOnScreen();
	}
	fListView->Invalidate();
}

void
IssuesContainerView::SetupViews(bool isReplicant)
{
	fRepositoryTitleView = new RepositoryTitleView(isReplicant);

	if (isReplicant == false) {
		SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
		fScrollView = new BScrollView("Scrollview", ListView(), 0, false, true, B_NO_BORDER);
	} else {
		SetViewColor(B_TRANSPARENT_COLOR);
		ListView()->SetViewColor(B_TRANSPARENT_COLOR);
	}

	BSize draggerSize = BSize(kDraggerSize,kDraggerSize);
	fDragger = new BDragger(this);

	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.AddGroup(B_VERTICAL, 0)
			.Add(fRepositoryTitleView)
			.Add(isReplicant ? static_cast<BView*>(ListView()) : static_cast<BView*>(fScrollView))
		.End()
		.AddGroup(B_HORIZONTAL)
			.AddGlue()
			.SetExplicitMinSize(draggerSize)
			.SetExplicitMaxSize(draggerSize)
			.Add(fDragger)
		.End()
	.End();
}
