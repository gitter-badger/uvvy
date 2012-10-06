#define VERSION	"0.01"

#include <cstdio>

#include <QHash>
#include <QMenu>
#include <QMenuBar>
#include <QToolBar>
#include <QTableView>
#include <QHeaderView>
#include <QPushButton>
#include <QStringListModel>
#include <QSystemTrayIcon>
#include <QCloseEvent>
#include <QVBoxLayout>
#include <QVBoxLayout>
#include <QMessageBox>
#include <QApplication>
#include <QSettings>
#include <QHostInfo>
#include <QDir>
#include <QtDebug>

#include "arch.h"
#include "env.h"
#include "main.h"
#include "voice.h"
#include "chat.h"
#include "search.h"
#include "regcli.h"
#include "peer.h"
#include "chunk.h"
#include "save.h"
#include "host.h"
#include "settings.h"

using namespace SST;


#define NCOLS		4
#define COL_TALK	2
#define COL_LISTEN	3


Host *ssthost;

MainWindow *mainwin;
PeerTable *friends;
VoiceService *talksrv;

QList<RegClient*> regclients;
RegInfo myreginfo;

QSettings *settings;
QDir appdir;
QFile logfile;

bool spewdebug;


void myMsgHandler(QtMsgType type, const char *msg)
{
	QTextStream strm(&logfile);
	switch (type) {
	case QtDebugMsg:
		strm << "Debug: " << msg << '\n';
		break;
	case QtWarningMsg:
		strm << "Warning: " << msg << '\n';
		break;
	case QtCriticalMsg:
		strm << "Critical: " << msg << '\n';
		strm.flush();
		QMessageBox::critical(NULL,
			QObject::tr("Netsteria: Critical Error"), msg,
			QMessageBox::Ok, QMessageBox::NoButton);
		break;
	case QtFatalMsg:
		strm << "Fatal: " << msg << '\n';
		strm.flush();
		QMessageBox::critical(NULL,
			QObject::tr("Netsteria: Critical Error"), msg,
			QMessageBox::Ok, QMessageBox::NoButton);
		abort();
	}
}

MainWindow::MainWindow()
:	searcher(NULL)
{
	QIcon appicon(":/img/netsteria.png");

	setWindowTitle(tr("Netsteria"));
	setWindowIcon(appicon);

	// Create a ListView onto our friends list, as the central widget
	Q_ASSERT(friends != NULL);
	friendslist = new QTableView(this);
	friendslist->setModel(friends);
	friendslist->setSelectionBehavior(QTableView::SelectRows);
	//friendslist->setStretchLastColumn(true);
	friendslist->setColumnWidth(0, 150);
	friendslist->setColumnWidth(1, 250);
	friendslist->setColumnWidth(2, 75);
	friendslist->setColumnHidden(1, true);
	friendslist->setColumnHidden(COL_LISTEN, true);	// XXX
	friendslist->verticalHeader()->hide();
	connect(friendslist, SIGNAL(clicked(const QModelIndex&)),
		this, SLOT(friendsClicked(const QModelIndex&)));
	connect(friendslist->selectionModel(),
		SIGNAL(currentChanged(const QModelIndex&, const QModelIndex&)),
		this, SLOT(updateMenus()));
	setCentralWidget(friendslist);

	// Create a "Friends" toolbar providing friends list controls
	// XX need icons
	QToolBar *friendsbar = new QToolBar(tr("Friends"), this);
	taMessage = friendsbar->addAction(tr("Message"),
					this, SLOT(openChat()));
	taTalk = friendsbar->addAction(tr("Talk"), this, SLOT(startTalk()));
	friendsbar->addSeparator();
	friendsbar->addAction(tr("My Profile"), this, SLOT(openProfile()));
	friendsbar->addAction(tr("Find Friends"), this, SLOT(openSearch()));
	taRename = friendsbar->addAction(tr("Rename"),
					this, SLOT(renameFriend()));
	taDelete = friendsbar->addAction(tr("Delete"),
					this, SLOT(deleteFriend()));
	addToolBar(friendsbar);

	// Create a "Friends" menu providing the same basic controls
	QMenu *friendsmenu = new QMenu(tr("Friends"), this);
	maMessage = friendsmenu->addAction(tr("&Message"),
				this, SLOT(openChat()),
				tr("Ctrl+M", "Friends|Message"));
	maTalk = friendsmenu->addAction(tr("&Talk"), this, SLOT(startTalk()),	
				tr("Ctrl+T", "Friends|Talk"));
	friendsmenu->addSeparator();
	friendsmenu->addAction(tr("&Find Friends"), this, SLOT(openSearch()),
				tr("Ctrl+F", "Friends|Find"));
	maRename = friendsmenu->addAction(tr("&Rename Friend"),
				this, SLOT(renameFriend()),
				tr("Ctrl+R", "Friends|Rename"));
	maDelete = friendsmenu->addAction(tr("&Delete Friend"),
				this, SLOT(deleteFriend()),
				tr("Ctrl+Delete", "Friends|Delete"));
	friendsmenu->addSeparator();
	friendsmenu->addAction(tr("&Settings..."), this,
				SLOT(openSettings()),
				tr("Ctrl+S", "Friends|Settings"));
	friendsmenu->addSeparator();
	friendsmenu->addAction(tr("E&xit"), this, SLOT(exitApp()),
				tr("Ctrl+X", "Friends|Exit"));
	menuBar()->addMenu(friendsmenu);

	// Create a "Window" menu
	QMenu *windowmenu = new QMenu(tr("Window"), this);
	windowmenu->addAction(tr("Friends"), this, SLOT(openFriends()));
	windowmenu->addAction(tr("Search"), this, SLOT(openSearch()));
	windowmenu->addAction(tr("Download"), this, SLOT(openDownload()));
	windowmenu->addAction(tr("Settings"), this, SLOT(openSettings()));
	menuBar()->addMenu(windowmenu);

	// Create a "Help" menu
	QMenu *helpmenu = new QMenu(tr("Help"), this);
	helpmenu->addAction(tr("Netsteria &Help"), this, SLOT(openHelp()),
				tr("Ctrl+H", "Help|Help"));
	helpmenu->addAction(tr("Netsteria Home Page"), this, SLOT(openWeb()));
	helpmenu->addAction(tr("About Netsteria..."), this, SLOT(openAbout()));
	//menuBar()->addMenu(helpmenu);

	// Watch for state changes that require updating the menus.
	connect(friendslist->selectionModel(),
		SIGNAL(currentChanged(const QModelIndex&, const QModelIndex&)),
		this, SLOT(updateMenus()));
	connect(talksrv, SIGNAL(statusChanged(const QByteArray&)),
		this, SLOT(updateMenus()));

	// Retrieve the main window settings
	settings->beginGroup("MainWindow");
	move(settings->value("pos", QPoint(100, 100)).toPoint());
	resize(settings->value("size", QSize(500, 300)).toSize());
	settings->endGroup();

	updateMenus();

	// Add a Netsteria icon to the system tray, if possible
	QSystemTrayIcon *trayicon = new QSystemTrayIcon(appicon, this);
	connect(trayicon, SIGNAL(activated(QSystemTrayIcon::ActivationReason)),
		this, SLOT(trayActivate(QSystemTrayIcon::ActivationReason)));
	trayicon->show();
}

MainWindow::~MainWindow()
{
	qDebug("MainWindow destructor");

	// Save the main window settings
	settings->beginGroup("MainWindow");
	settings->setValue("pos", pos());
	settings->setValue("size", size());
	settings->endGroup();
}

void MainWindow::trayActivate(QSystemTrayIcon::ActivationReason reason)
{
	qDebug("MainWindow::trayActivate - reason %d", (int)reason);
	openFriends();
}

int MainWindow::selectedFriend()
{
	return friendslist->selectionModel()->currentIndex().row();
}

void MainWindow::updateMenus()
{
	int row = selectedFriend();
	bool sel = isActiveWindow() && row >= 0;
	QByteArray id = sel ? friends->id(row) : QByteArray();

	maMessage->setEnabled(sel);
	taMessage->setEnabled(sel);
	maTalk->setEnabled(sel && talksrv->outConnected(id));
	taTalk->setEnabled(sel && talksrv->outConnected(id));
	maRename->setEnabled(sel);
	taRename->setEnabled(sel);
	maDelete->setEnabled(sel);
	taDelete->setEnabled(sel);
}

bool MainWindow::event(QEvent *event)
{
	switch (event->type()) {
	case QEvent::WindowActivate:
	case QEvent::WindowDeactivate:
		updateMenus();
		break;
	default:
		break;
	}

	return QMainWindow::event(event);
}

void MainWindow::friendsClicked(const QModelIndex &index)
{
	int row = index.row();
	if (row < 0 || row >= friends->count())
		return;
	QByteArray hostid = friends->id(row);

	int col = index.column();
	if (col == COL_TALK)
		talksrv->toggleTalkEnabled(hostid);
}

void MainWindow::openFriends()
{
	show();
	raise();
	activateWindow();
}

void MainWindow::openSearch()
{
	if (!searcher)
		searcher = new SearchDialog(this);
	searcher->present();
}

void MainWindow::openDownload()
{
	SaveDialog::present();
}

void MainWindow::openChat()
{
	int row = friendslist->selectionModel()->currentIndex().row();
	if (row < 0 || row >= friends->count())
		return;

	ChatDialog::open(friends->id(row), friends->name(row));
}

void MainWindow::startTalk()
{
	int row = selectedFriend();
	if (row <= 0 || row >= friends->count())
		return;

	talksrv->toggleTalkEnabled(friends->id(row));
}

void MainWindow::openSettings()
{
	SettingsDialog::openSettings();
}

void MainWindow::openProfile()
{
	SettingsDialog::openProfile();
}

void MainWindow::openHelp()
{
}

void MainWindow::openWeb()
{
}

void MainWindow::openAbout()
{
	QMessageBox *mbox = new QMessageBox(tr("About Netsteria"),
				tr("Netsteria version %0\n"
				   "Copyright 2006 Bryan Ford").arg(VERSION),
				QMessageBox::Information,
				QMessageBox::Ok, QMessageBox::NoButton,
				QMessageBox::NoButton, this);
	mbox->show();
	//mbox->setAttribute(Qt::WA_DeleteOnClose, true);
}

void MainWindow::renameFriend()
{
	int row = friendslist->selectionModel()->currentIndex().row();
	if (row < 0 || row >= friends->count())
		return;

	friendslist->edit(friends->index(row, 0));
}

void MainWindow::deleteFriend()
{
	int row = friendslist->selectionModel()->currentIndex().row();
	if (row < 0 || row >= friends->count())
		return;
	QString name = friends->name(row);
	QByteArray id = friends->id(row);

	if (QMessageBox::question(this, tr("Confirm Delete"),
			tr("Delete '%0' from your contacts?").arg(name),
			QMessageBox::Yes | QMessageBox::Default,
			QMessageBox::No | QMessageBox::Escape)
			!= QMessageBox::Yes)
		return;

	qDebug() << "Removing" << name;
	friends->remove(id);
}

void MainWindow::addPeer(const QByteArray &id, QString name, bool edit)
{
	int row = friends->insert(id, name);

	if (edit) {
		//qDebug() << "Edit friend" << row;
		//friendslist->setCurrentIndex(peersmodel->index(row, 0));
		friendslist->edit(friends->index(row, 0));
	}
}

void MainWindow::closeEvent(QCloseEvent *event)
{
	qDebug("MainWindow close");

	// If there's no system tray, exit immediately
	// because the user would have no way to get us open again.
	if (!QSystemTrayIcon::isSystemTrayAvailable())
		exitApp();

	// Hide our main window but otherwise ignore the event,
	// waiting quietly in the background until we're re-activated.
	event->ignore();
	hide();
}

void MainWindow::exitApp()
{
	qDebug("MainWindow exit");

	QApplication::exit(0);
}

static void regcli(const QString &hostname)
{
	RegClient *regcli = new RegClient(ssthost);
	regcli->setInfo(myreginfo);
	regcli->registerAt(hostname);
	regclients.append(regcli);
}

static void usage()
{
	qCritical("Usage: netsteria [-d] [<configdir>]\n"
		"  -d              Enable debugging output\n"
		"  <configdir>     Optional config state directory\n");
	exit(1);
}

int main(int argc, char **argv)
{
	QApplication app(argc, argv);
	app.setOrganizationName("MIT-PDOS");
	app.setApplicationName("Netsteria");
	app.setQuitOnLastWindowClosed(false);

	while (argc > 1 && argv[1][0] == '-') {
		switch (argv[1][1]) {
		case 'd':
			spewdebug = true;
			break;
		default:
			usage();
		}
		argc--, argv++;
	}

	if (argc > 1) {
		if (argc > 2 || argv[1][0] == '-') {
			usage();
		}
		QDir::current().mkdir(argv[1]);
		appdir.setPath(argv[1]);

		settings = new QSettings(appdir.path() + "/config",
					QSettings::IniFormat);
		if (settings->status() != QSettings::NoError) {
			qFatal("Can't open config file in dir '%s'\n",
				argv[1]);
		}
	} else {
		QDir homedir = QDir::home();
		QString homedirpath = homedir.path();

		QString appdirname = ".netsteria";
		homedir.mkdir(appdirname);
		appdir.setPath(homedirpath + "/" + appdirname);

		settings = new QSettings();
	}

	// Send debugging output to a log file
	QString logname(appdir.path() + "/log");
	QString logbakname(appdir.path() + "/log.bak");
	QFile::remove(logbakname);
	QFile::rename(logname, logbakname);
	logfile.setFileName(logname);
	if (!logfile.open(QFile::WriteOnly | QFile::Truncate))
		qWarning("Can't open log file '%s'",
			logname.toLocal8Bit().data());
	else
		qInstallMsgHandler(myMsgHandler);
	qDebug() << "Netsteria starting";

#if 0
//	openDefaultLog();

//	assert(argc == 3);	// XXX
//	mydev.setUserName(QString::fromAscii(argv[1]));
//	mydev.setDevName(QString::fromAscii(argv[2]));
	mydev.setUserName(QString::fromAscii("Bob"));
	mydev.setDevName(QString::fromAscii("phone"));

	keyinit();
	mydev.setEID(mykey->eid);

	netinit();

	mainwin = new MainWindow;
	return mainwin->exec();
#endif

	// Initialize the Structured Stream Transport
	ssthost = new Host(settings, NETSTERIA_DEFAULT_PORT);


	// Initialize the settings system, read user profile
	SettingsDialog::init();

	// XXX user info dialog
	myreginfo.setHostName(profile->hostName());
	myreginfo.setOwnerName(profile->ownerName());
	myreginfo.setCity(profile->city());
	myreginfo.setRegion(profile->region());
	myreginfo.setCountry(profile->country());
	myreginfo.setEndpoints(ssthost->activeLocalEndpoints());
	qDebug() << "local endpoints" << myreginfo.endpoints().size();

	// XXX allow user-modifiable set of regservers
	regcli("xi.lcs.mit.edu");
	regcli("pdos.csail.mit.edu");

	// Load and initialize our friends table
	friends = new PeerTable(NCOLS);
	friends->setHeaderData(COL_TALK, Qt::Horizontal,
				QObject::tr("Talk"), Qt::DisplayRole);
	friends->setHeaderData(COL_LISTEN, Qt::Horizontal,
				QObject::tr("Listen"), Qt::DisplayRole);
	friends->useSettings(settings, "Friends");

	// Initialize our chunk sharing service
	ChunkShare::instance()->setPeerTable(friends);

	talksrv = new VoiceService();
	talksrv->setPeerTable(friends);
	talksrv->setTalkColumn(2);
	talksrv->setListenColumn(3);

	// Start our chat server to accept chat connections
	ChatServer *chatsrv = new ChatServer();
	(void)chatsrv;

	// Re-start incomplete downloads
	SaveDialog::init();

	mainwin = new MainWindow;
	mainwin->show();
	return app.exec();
}
