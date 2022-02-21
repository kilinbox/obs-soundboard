#include <obs-frontend-api.h>
#include <obs-module.h>
#include "util/platform.h"
#include "obs-hotkey.h"
#include "util/util.hpp"
#include "plugin-macros.generated.h"
#include "soundboard.hpp"
#include "adv-audio-control.hpp"
#include <QAction>
#include <QMainWindow>
#include <QObject>
#include <QFileDialog>
#include <QStandardPaths>
#include <QMessageBox>
#include <QDockWidget>
#include <QMenu>
#include <QAction>

#define QT_UTF8(str) QString::fromUtf8(str, -1)
#define QT_TO_UTF8(str) str.toUtf8().constData()
#define QTStr(str) QT_UTF8(obs_module_text(str))

ListRenameDelegate::ListRenameDelegate(QObject *parent)
	: QStyledItemDelegate(parent)
{
}

void ListRenameDelegate::setEditorData(QWidget *editor,
				       const QModelIndex &index) const
{
	QStyledItemDelegate::setEditorData(editor, index);
	QLineEdit *lineEdit = qobject_cast<QLineEdit *>(editor);
	if (lineEdit)
		lineEdit->selectAll();
}

bool ListRenameDelegate::eventFilter(QObject *editor, QEvent *event)
{
	if (event->type() == QEvent::KeyPress) {
		QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);
		if (keyEvent->key() == Qt::Key_Escape) {
			QLineEdit *lineEdit = qobject_cast<QLineEdit *>(editor);
			if (lineEdit)
				lineEdit->undo();
		}
	}

	return QStyledItemDelegate::eventFilter(editor, event);
}

SoundEdit::SoundEdit(QWidget *parent) : QDialog(parent), ui(new Ui_SoundEdit)
{
	ui->setupUi(this);
	ui->fileEdit->setReadOnly(true);

	ui->nameLabel->setText(QTStr("Name"));
	ui->fileLabel->setText(QTStr("File"));
	ui->browseButton->setText(QTStr("Browse"));
}

void SoundEdit::on_browseButton_clicked()
{
	QString folder = ui->fileEdit->text();

	if (!folder.isEmpty())
		folder = QFileInfo(folder).absoluteDir().absolutePath();
	else
		folder = QStandardPaths::writableLocation(
			QStandardPaths::HomeLocation);

	QString fileName = QFileDialog::getOpenFileName(
		this, QTStr("OpenAudioFile"), folder,
		("Audio (*.mp3 *.aac *.ogg *.wav *.flac)"));

	if (!fileName.isEmpty())
		ui->fileEdit->setText(fileName);
}

static bool NameExists(QListWidget *list, const QString &name)
{
	for (int i = 0; i < list->count(); i++) {
		if (list->item(i)->text() == name)
			return true;
	}

	return false;
}

void SoundEdit::on_buttonBox_clicked(QAbstractButton *button)
{
	Soundboard *sb = static_cast<Soundboard *>(parent());

	if (!sb)
		return;

	QDialogButtonBox::ButtonRole val = ui->buttonBox->buttonRole(button);

	if (val == QDialogButtonBox::RejectRole) {
		goto close;
	} else if (val == QDialogButtonBox::AcceptRole) {
		if (ui->nameEdit->text() == "") {
			QMessageBox::warning(this, QTStr("EmptyName.Title"),
					     QTStr("EmptyName.Text"));
			return;
		}

		bool nameExists = false;

		if ((editMode && ui->nameEdit->text() != origText &&
		     NameExists(sb->ui->soundList, ui->nameEdit->text())) ||
		    (!editMode &&
		     NameExists(sb->ui->soundList, ui->nameEdit->text())))
			nameExists = true;

		if (nameExists) {
			QMessageBox::warning(this, QTStr("NameExists.Title"),
					     QTStr("NameExists.Text"));
			return;
		}

		if (ui->fileEdit->text() == "") {
			QMessageBox::warning(this, QTStr("NoFile.Title"),
					     QTStr("NoFile.Text"));
			return;
		}

		if (editMode)
			sb->EditSound(ui->nameEdit->text(),
				      ui->fileEdit->text());
		else
			sb->AddSound(ui->nameEdit->text(), ui->fileEdit->text(),
				     nullptr);
	}

close:
	close();
}

static void OnSave(obs_data_t *saveData, bool saving, void *data)
{
	Soundboard *sb = static_cast<Soundboard *>(data);

	if (saving)
		sb->SaveSoundboard(saveData);
	else
		sb->LoadSoundboard(saveData);
}

static void OBSEvent(enum obs_frontend_event event, void *data)
{
	Soundboard *sb = static_cast<Soundboard *>(data);
	QDockWidget *dock = static_cast<QDockWidget *>(sb->parent());
	obs_source_t *source = nullptr;

	switch (event) {
	case OBS_FRONTEND_EVENT_FINISHED_LOADING:
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED:
		if (!sb->source) {
			source = obs_source_create_private(
				"ffmpeg_source",
				QT_TO_UTF8(QTStr("Soundboard")), nullptr);
			sb->vol.reset(new VolControl(source, true, true));
			QObject::connect(sb->vol.data()->config,
					 SIGNAL(clicked()), sb,
					 SLOT(VolControlContextMenu()));
			sb->ui->horizontalLayout->addWidget(sb->vol.data());
			sb->mediaControls->SetSource(source);
			sb->ResetWidgets();
			sb->source = source;
		}

		obs_set_output_source(8, sb->source);

		if (event == OBS_FRONTEND_EVENT_FINISHED_LOADING)
			obs_frontend_add_dock(dock);

		break;
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CLEANUP:
		sb->ClearSoundboard();
	default:
		break;
	};
}

static QListWidgetItem *FindItem(QListWidget *list, const QString &name)
{
	for (int i = 0; i < list->count(); i++) {
		QListWidgetItem *item = list->item(i);

		if (item->text() == name)
			return item;
	}

	return nullptr;
}

Soundboard::Soundboard(QWidget *parent) : QWidget(parent), ui(new Ui_Soundboard)
{
	ui->setupUi(this);
	ui->soundList->SetGridMode(true);

	setObjectName("Soundboard");

	for (QAction *x : ui->soundboardToolbar->actions()) {
		QWidget *temp = ui->soundboardToolbar->widgetForAction(x);

		for (QByteArray &y : x->dynamicPropertyNames()) {
			temp->setProperty(y, x->property(y));
		}
	}

	ui->actionAddSound->setToolTip(QTStr("AddSound"));
	ui->actionRemoveSound->setToolTip(QTStr("RemoveSound.Title"));
	ui->actionEditSound->setToolTip(QTStr("EditSound"));

	auto stop = [](void *data, obs_hotkey_id, obs_hotkey_t *,
		       bool pressed) {
		Soundboard *sb = static_cast<Soundboard *>(data);

		if (pressed)
			QMetaObject::invokeMethod(sb, "StopSound",
						  Qt::QueuedConnection);
	};

	stopHotkey = obs_hotkey_register_frontend(
		"Soundboard.Stop", QT_TO_UTF8(QTStr("StopHotkey")), stop, this);

	auto restart = [](void *data, obs_hotkey_id, obs_hotkey_t *,
			  bool pressed) {
		Soundboard *sb = static_cast<Soundboard *>(data);

		if (pressed)
			QMetaObject::invokeMethod(sb, "RestartSound",
						  Qt::QueuedConnection);
	};

	restartHotkey = obs_hotkey_register_frontend(
		"Soundboard.Restart", QT_TO_UTF8(QTStr("RestartHotkey")),
		restart, this);

	auto mute = [](void *data, obs_hotkey_pair_id, obs_hotkey_t *,
		       bool pressed) {
		Soundboard *sb = static_cast<Soundboard *>(data);
		bool muted = obs_source_muted(sb->source);

		if (!muted && pressed) {
			QMetaObject::invokeMethod(sb, "SoundboardSetMuted",
						  Qt::QueuedConnection,
						  Q_ARG(bool, true));

			return true;
		}

		return false;
	};

	auto unmute = [](void *data, obs_hotkey_pair_id, obs_hotkey_t *,
			 bool pressed) {
		Soundboard *sb = static_cast<Soundboard *>(data);
		bool muted = obs_source_muted(sb->source);

		if (muted && pressed) {
			QMetaObject::invokeMethod(sb, "SoundboardSetMuted",
						  Qt::QueuedConnection,
						  Q_ARG(bool, false));

			return true;
		}

		return false;
	};

	muteHotkeys = obs_hotkey_pair_register_frontend(
		"Soundboard.Mute", QT_TO_UTF8(QTStr("MuteHotkey")),
		"Soundboard.Unmute", QT_TO_UTF8(QTStr("UnmuteHotkey")), mute,
		unmute, this, this);

	auto play = [](void *data, obs_hotkey_pair_id, obs_hotkey_t *,
		       bool pressed) {
		Soundboard *sb = static_cast<Soundboard *>(data);
		bool paused = sb->mediaControls->MediaPaused();

		if (paused && pressed) {
			QMetaObject::invokeMethod(sb, "PlaySound",
						  Qt::QueuedConnection);
			return true;
		}

		return false;
	};

	auto pause = [](void *data, obs_hotkey_pair_id, obs_hotkey_t *,
			bool pressed) {
		Soundboard *sb = static_cast<Soundboard *>(data);
		bool paused = sb->mediaControls->MediaPaused();

		if (!paused && pressed) {
			QMetaObject::invokeMethod(sb, "PauseSound",
						  Qt::QueuedConnection);

			return true;
		}

		return false;
	};

	playPauseHotkeys = obs_hotkey_pair_register_frontend(
		"Soundboard.Play", QT_TO_UTF8(QTStr("PlayHotkey")),
		"Soundboard.Pause", QT_TO_UTF8(QTStr("PauseHotkey")), play,
		pause, this, this);

	ui->soundList->setItemDelegate(new ListRenameDelegate(ui->soundList));

	connect(ui->soundList->itemDelegate(),
		SIGNAL(closeEditor(QWidget *,
				   QAbstractItemDelegate::EndEditHint)),
		this,
		SLOT(SoundListNameEdited(QWidget *,
					 QAbstractItemDelegate::EndEditHint)));

	QAction *renameSound = new QAction(ui->soundList);
	renameSound->setShortcutContext(Qt::WidgetWithChildrenShortcut);
	connect(renameSound, SIGNAL(triggered()), this,
		SLOT(EditSoundListItem()));
	ui->soundList->addAction(renameSound);

#ifdef __APPLE__
	renameSound->setShortcut({Qt::Key_Return});
#else
	renameSound->setShortcut({Qt::Key_F2});
#endif

	mediaControls = new MediaControls();
	ui->mainLayout->insertWidget(1, mediaControls);

	obs_frontend_add_event_callback(OBSEvent, this);
	obs_frontend_add_save_callback(OnSave, this);
}

Soundboard::~Soundboard()
{
	obs_hotkey_unregister(stopHotkey);
	obs_hotkey_unregister(restartHotkey);
	obs_hotkey_pair_unregister(muteHotkeys);
	obs_hotkey_pair_unregister(playPauseHotkeys);
	obs_frontend_remove_event_callback(OBSEvent, this);
	obs_frontend_remove_save_callback(OnSave, this);
}

obs_data_array_t *Soundboard::SaveSounds()
{
	obs_data_array_t *soundArray = obs_data_array_create();

	for (int i = 0; i < ui->soundList->count(); i++) {
		QListWidgetItem *item = ui->soundList->item(i);
		SoundData *sound = FindSoundByName(item->text());

		OBSDataAutoRelease data = obs_data_create();
		obs_data_set_string(data, "name",
				    QT_TO_UTF8(GetSoundName(sound)));
		obs_data_set_string(data, "path",
				    QT_TO_UTF8(GetSoundPath(sound)));

		OBSDataArrayAutoRelease hotkeyArray =
			obs_hotkey_save(GetSoundHotkey(sound));
		obs_data_set_array(data, "sound_hotkey", hotkeyArray);

		obs_data_array_push_back(soundArray, data);
	}

	return soundArray;
}

void Soundboard::LoadSounds(obs_data_array_t *array)
{
	size_t num = obs_data_array_count(array);

	for (size_t i = 0; i < num; i++) {
		OBSDataAutoRelease data = obs_data_array_item(array, i);
		const char *name = obs_data_get_string(data, "name");
		const char *path = obs_data_get_string(data, "path");

		AddSound(QT_UTF8(name), QT_UTF8(path), data);
	}
}

void Soundboard::SaveSoundboard(obs_data_t *saveData)
{
	QMainWindow *window = (QMainWindow *)obs_frontend_get_main_window();
	QDockWidget *dock = static_cast<QDockWidget *>(parent());

	OBSDataArrayAutoRelease array = SaveSounds();
	obs_data_set_array(saveData, "soundboard_array", array);

	if (source) {
		OBSDataAutoRelease sourceData = obs_save_source(source);
		obs_data_set_obj(saveData, "soundboard_source", sourceData);
	}

	OBSDataArrayAutoRelease hotkeyArray = obs_hotkey_save(stopHotkey);
	obs_data_set_array(saveData, "stop_hotkey", hotkeyArray);

	hotkeyArray = obs_hotkey_save(restartHotkey);
	obs_data_set_array(saveData, "restart_hotkey", hotkeyArray);

	obs_data_array_t *data0 = nullptr;
	obs_data_array_t *data1 = nullptr;

	obs_hotkey_pair_save(muteHotkeys, &data0, &data1);
	obs_data_set_array(saveData, "mute_hotkey", data0);
	obs_data_set_array(saveData, "unmute_hotkey", data1);

	obs_data_array_release(data0);
	obs_data_array_release(data1);

	obs_data_array_t *data2 = nullptr;
	obs_data_array_t *data3 = nullptr;

	obs_hotkey_pair_save(playPauseHotkeys, &data2, &data3);
	obs_data_set_array(saveData, "play_hotkey", data2);
	obs_data_set_array(saveData, "pause_hotkey", data3);

	obs_data_array_release(data2);
	obs_data_array_release(data3);

	obs_data_set_bool(saveData, "visible", dock->isVisible());
	obs_data_set_bool(saveData, "floating", dock->isFloating());
	obs_data_set_string(saveData, "geometry",
			    dock->saveGeometry().toBase64().constData());
	obs_data_set_int(saveData, "dock_area", window->dockWidgetArea(dock));
	obs_data_set_bool(saveData, "grid_mode", ui->soundList->GetGridMode());
	obs_data_set_bool(saveData, "lock_volume", lockVolume);

	if (vol.data())
		obs_data_set_bool(saveData, "hide_volume",
				  !vol.data()->isVisible());

	obs_data_set_bool(saveData, "hide_media_controls",
			  !mediaControls->isVisible());
	obs_data_set_bool(saveData, "hide_toolbar",
			  !ui->soundboardToolbar->isVisible());

	obs_data_set_bool(saveData, "props_use_percent", usePercent);

	QListWidgetItem *item = ui->soundList->currentItem();

	if (item)
		obs_data_set_string(saveData, "current_sound",
				    QT_TO_UTF8(item->text()));

	obs_data_set_bool(saveData, "use_countdown",
			  mediaControls->countDownTimer);
}

void Soundboard::LoadSoundboard(obs_data_t *saveData)
{
	QMainWindow *window = (QMainWindow *)obs_frontend_get_main_window();
	QDockWidget *dock = static_cast<QDockWidget *>(parent());

	OBSDataAutoRelease sourceData =
		obs_data_get_obj(saveData, "soundboard_source");

	obs_source_t *newSource = obs_source_create_private(
		"ffmpeg_source", QT_TO_UTF8(QTStr("Soundboard")), nullptr);

	if (!obs_obj_invalid(newSource)) {
		vol.reset(new VolControl(newSource, true, true));
		QObject::connect(vol.data()->config, SIGNAL(clicked()), this,
				 SLOT(VolControlContextMenu()));
		ui->horizontalLayout->addWidget(vol.data());
		mediaControls->SetSource(newSource);
		source = newSource;

		obs_source_set_muted(source,
				     obs_data_get_bool(sourceData, "muted"));

		obs_data_set_default_double(sourceData, "volume", 1.0);
		obs_source_set_volume(source, (float)obs_data_get_double(
						      sourceData, "volume"));

		obs_data_set_default_double(sourceData, "balance", 0.5);
		obs_source_set_balance_value(
			source,
			(float)obs_data_get_double(sourceData, "balance"));

		obs_source_set_sync_offset(source, obs_data_get_int(sourceData,
								    "sync"));

		obs_source_set_monitoring_type(
			source, (enum obs_monitoring_type)obs_data_get_int(
					sourceData, "monitoring_type"));

		obs_data_set_default_int(sourceData, "mixers", 0x3F);
		uint32_t mixers =
			(uint32_t)obs_data_get_int(sourceData, "mixers");
		obs_source_set_audio_mixers(source, mixers);

		uint32_t flags =
			(uint32_t)obs_data_get_int(sourceData, "flags");
		obs_source_set_flags(source, flags);

		OBSDataArrayAutoRelease filters =
			obs_data_get_array(sourceData, "filters");

		if (filters) {
			size_t count = obs_data_array_count(filters);

			for (size_t i = 0; i < count; i++) {
				OBSDataAutoRelease filterData =
					obs_data_array_item(filters, i);

				OBSSourceAutoRelease filter =
					obs_load_source(filterData);

				if (filter)
					obs_source_filter_add(source, filter);
			}
		}
	}

	OBSDataArrayAutoRelease array =
		obs_data_get_array(saveData, "soundboard_array");
	LoadSounds(array);

	OBSDataArrayAutoRelease hotkeyArray =
		obs_data_get_array(saveData, "stop_hotkey");
	obs_hotkey_load(stopHotkey, hotkeyArray);

	hotkeyArray = obs_data_get_array(saveData, "restart_hotkey");
	obs_hotkey_load(restartHotkey, hotkeyArray);

	OBSDataArrayAutoRelease data0 =
		obs_data_get_array(saveData, "mute_hotkey");
	OBSDataArrayAutoRelease data1 =
		obs_data_get_array(saveData, "unmute_hotkey");
	obs_hotkey_pair_load(muteHotkeys, data0, data1);

	data0 = obs_data_get_array(saveData, "play_hotkey");
	data1 = obs_data_get_array(saveData, "pause_hotkey");
	obs_hotkey_pair_load(playPauseHotkeys, data0, data1);

	const char *geometry = obs_data_get_string(saveData, "geometry");

	if (geometry && *geometry)
		dock->restoreGeometry(
			QByteArray::fromBase64(QByteArray(geometry)));

	const auto dockArea = static_cast<Qt::DockWidgetArea>(
		obs_data_get_int(saveData, "dock_area"));

	if (dockArea)
		window->addDockWidget(dockArea, dock);

	bool visible = obs_data_get_bool(saveData, "visible");
	dock->setVisible(visible);

	obs_data_set_default_bool(saveData, "floating", true);
	bool floating = obs_data_get_bool(saveData, "floating");
	dock->setFloating(floating);

	obs_data_set_default_bool(saveData, "grid_mode", true);
	bool grid = obs_data_get_bool(saveData, "grid_mode");
	ui->soundList->SetGridMode(grid);

	bool lock = obs_data_get_bool(saveData, "lock_volume");
	ToggleLockVolume(lock);

	bool hide = obs_data_get_bool(saveData, "hide_volume");
	vol.data()->setHidden(hide);

	hide = obs_data_get_bool(saveData, "hide_media_controls");
	mediaControls->setHidden(hide);

	hide = obs_data_get_bool(saveData, "hide_toolbar");
	ui->soundboardToolbar->setHidden(hide);

	bool percent = obs_data_get_bool(saveData, "props_use_percent");
	usePercent = percent;

	QString lastSound =
		QT_UTF8(obs_data_get_string(saveData, "current_sound"));

	if (!lastSound.isEmpty()) {
		QListWidgetItem *item = FindItem(ui->soundList, lastSound);

		if (item)
			ui->soundList->setCurrentItem(item);
		else
			ui->soundList->setCurrentRow(0);
	} else {
		ui->soundList->setCurrentRow(0);
	}

	bool countdown = obs_data_get_bool(saveData, "use_countdown");
	mediaControls->countDownTimer = countdown;
}

void Soundboard::ClearSoundboard()
{
	mediaControls->countDownTimer = false;
	mediaControls->SetSource(nullptr);
	source = nullptr;

	ui->soundList->setCurrentItem(nullptr, QItemSelectionModel::Clear);

	for (int i = 0; i < ui->soundList->count(); i++) {
		QListWidgetItem *item = ui->soundList->item(i);
		delete item;
		item = nullptr;
	}

	for (SoundData *sound : sounds) {
		obs_hotkey_unregister(sound->hotkey);
		delete sound;
		sound = nullptr;
	}

	sounds.clear();
	ui->soundList->clear();
}

void Soundboard::PlaySound(const QString &name)
{
	SoundData *sound = FindSoundByName(name);

	if (!sound)
		return;

	OBSDataAutoRelease settings = obs_source_get_settings(source);
	QString path = GetSoundPath(sound);
	QString lastPath = QT_UTF8(obs_data_get_string(settings, "local_file"));
	QListWidgetItem *item = FindItem(ui->soundList, name);

	if (!os_file_exists(QT_TO_UTF8(path))) {
		ui->soundList->setCurrentItem(item);
		QMessageBox::critical(this, QTStr("FileNotFound.Title"),
				      QTStr("FileNotFound.Text"));
		return;
	}

	if (lastPath == path) {
		obs_source_media_restart(source);
		ui->soundList->setCurrentItem(item);
		return;
	}

	settings = obs_data_create();
	obs_data_set_string(settings, "local_file", QT_TO_UTF8(path));
	obs_data_set_bool(settings, "is_local_file", true);
	obs_source_update(source, settings);

	ui->soundList->setCurrentItem(item);
}

static void PlaySoundHotkey(void *data, obs_hotkey_id, obs_hotkey_t *,
			    bool pressed)
{
	SoundData *sound = static_cast<SoundData *>(data);

	QMainWindow *window = (QMainWindow *)obs_frontend_get_main_window();
	Soundboard *sb = window->findChild<Soundboard *>("Soundboard");

	if (pressed)
		QMetaObject::invokeMethod(sb, "PlaySound", Qt::QueuedConnection,
					  Q_ARG(QString, sound->name));
}

void Soundboard::AddSound(const QString &name, const QString &path,
			  obs_data_t *settings)
{
	if (name.isEmpty() || path.isEmpty())
		return;

	QString hotkeyName = QTStr("SoundHotkey").arg(name);

	QListWidgetItem *item = new QListWidgetItem(name);

	struct SoundData *sound = new SoundData();
	sound->path = path;
	sound->name = name;
	sound->hotkey = obs_hotkey_register_frontend(QT_TO_UTF8(hotkeyName),
						     QT_TO_UTF8(hotkeyName),
						     PlaySoundHotkey, sound);

	sounds.emplace_back(sound);

	if (settings) {
		OBSDataArrayAutoRelease hotkeyArray =
			obs_data_get_array(settings, "sound_hotkey");
		obs_hotkey_load(sound->hotkey, hotkeyArray);
	}

	ui->soundList->addItem(item);
	ui->soundList->setCurrentItem(item);
}

void Soundboard::EditSound(const QString &newName, const QString &newPath)
{
	if (newName.isEmpty() || newPath.isEmpty())
		return;

	QListWidgetItem *si = ui->soundList->currentItem();

	if (!si)
		return;

	SoundData *sound = FindSoundByName(si->text());

	SetSoundPath(sound, newPath);
	SetSoundName(sound, newName);

	si->setText(newName);

	QString hotkeyName = QTStr("SoundHotkey").arg(newName);
	obs_hotkey_id hotkey = GetSoundHotkey(sound);
	obs_hotkey_set_name(hotkey, QT_TO_UTF8(hotkeyName));
	obs_hotkey_set_description(hotkey, QT_TO_UTF8(hotkeyName));
}

static QString GetDefaultString(QListWidget *list, const QString &name)
{
	if (!NameExists(list, name))
		return name;

	int i = 2;

	for (;;) {
		QString out = name + " " + QString::number(i);

		if (!NameExists(list, out))
			return out;

		i++;
	}
}

void Soundboard::on_actionAddSound_triggered()
{
	SoundEdit edit(this);
	edit.ui->nameEdit->setText(
		GetDefaultString(ui->soundList, QTStr("Sound")));
	edit.setWindowTitle(QTStr("AddSound"));
	edit.exec();
}

void Soundboard::on_actionEditSound_triggered()
{
	QListWidgetItem *si = ui->soundList->currentItem();

	if (!si)
		return;

	SoundEdit edit(this);
	edit.setWindowTitle(QTStr("EditSound"));
	edit.origText = si->text();
	edit.editMode = true;

	SoundData *sound = FindSoundByName(si->text());
	edit.ui->nameEdit->setText(sound->name);
	edit.ui->fileEdit->setText(sound->path);

	edit.exec();

	edit.origText = "";
}

void Soundboard::on_soundList_itemClicked(QListWidgetItem *item)
{
	PlaySound(item->text());
}

void Soundboard::on_actionRemoveSound_triggered()
{
	QListWidgetItem *si = ui->soundList->currentItem();

	if (!si)
		return;

	QMessageBox::StandardButton reply =
		QMessageBox::question(this, QTStr("RemoveSound.Title"),
				      QTStr("RemoveSound.Text").arg(si->text()),
				      QMessageBox::Yes | QMessageBox::No);

	if (reply == QMessageBox::No)
		return;

	for (size_t i = 0; i < sounds.size(); i++) {
		SoundData *sound = sounds.at(i);

		if (sound->name == si->text()) {
			obs_hotkey_unregister(sound->hotkey);

			delete sound;
			sound = nullptr;
			sounds.erase(sounds.begin() + i);
			break;
		}
	}

	delete si;
	si = nullptr;
}

void Soundboard::PlaySound()
{
	obs_source_media_play_pause(source, false);
}

void Soundboard::PauseSound()
{
	obs_source_media_play_pause(source, true);
}

void Soundboard::StopSound()
{
	obs_source_media_stop(source);
}

void Soundboard::RestartSound()
{
	obs_source_media_restart(source);
}

void Soundboard::SetGrid()
{
	bool grid = ui->soundList->GetGridMode();
	ui->soundList->SetGridMode(!grid);
}

void Soundboard::DuplicateSound()
{
	QListWidgetItem *si = ui->soundList->currentItem();

	if (!si)
		return;

	SoundData *sound = FindSoundByName(si->text());
	AddSound(GetDefaultString(ui->soundList, GetSoundName(sound)),
		 GetSoundPath(sound));
}

void Soundboard::ResetWidgets()
{
	ui->soundList->SetGridMode(true);

	if (vol.data())
		vol.data()->show();

	if (mediaControls)
		mediaControls->show();

	ui->soundboardToolbar->show();
}

void Soundboard::VolumeControlsToggled(bool checked)
{
	if (vol.data())
		vol.data()->setVisible(checked);
}

void Soundboard::MediaControlsToggled(bool checked)
{
	mediaControls->setVisible(checked);
}

void Soundboard::ToolbarToggled(bool checked)
{
	ui->soundboardToolbar->setVisible(checked);
}

void Soundboard::on_soundList_customContextMenuRequested(const QPoint &pos)
{
	QListWidgetItem *item = ui->soundList->itemAt(pos);

	bool grid = ui->soundList->GetGridMode();

	QMenu view(QTStr("View"), this);
	view.addAction(QTStr("Reset"), this, SLOT(ResetWidgets()));
	view.addSeparator();
	view.addAction(grid ? QTStr("ListMode") : QTStr("GridMode"), this,
		       SLOT(SetGrid()));
	view.addSeparator();

	QAction volumeAction(QTStr("VolumeControls"));
	volumeAction.setCheckable(true);
	volumeAction.setChecked(vol.data()->isVisible());
	connect(&volumeAction, SIGNAL(toggled(bool)), this,
		SLOT(VolumeControlsToggled(bool)));
	view.addAction(&volumeAction);

	QAction mediaAction(QTStr("MediaControls"));
	mediaAction.setCheckable(true);
	mediaAction.setChecked(mediaControls->isVisible());
	connect(&mediaAction, SIGNAL(toggled(bool)), this,
		SLOT(MediaControlsToggled(bool)));
	view.addAction(&mediaAction);

	QAction toolbarAction(QTStr("Toolbar"));
	toolbarAction.setCheckable(true);
	toolbarAction.setChecked(ui->soundboardToolbar->isVisible());
	connect(&toolbarAction, SIGNAL(toggled(bool)), this,
		SLOT(ToolbarToggled(bool)));
	view.addAction(&toolbarAction);

	QMenu popup(this);

	popup.addAction(QTStr("Add"), this,
			SLOT(on_actionAddSound_triggered()));
	popup.addSeparator();

	if (item) {
		popup.addAction(QTStr("Edit"), this,
				SLOT(on_actionEditSound_triggered()));
		popup.addAction(QTStr("Remove"), this,
				SLOT(on_actionRemoveSound_triggered()));
		popup.addAction(QTStr("Duplicate"), this,
				SLOT(DuplicateSound()));
		popup.addSeparator();
	}

	popup.addSeparator();

	popup.addAction(QTStr("Filters"), this, SLOT(OpenFilters()));
	popup.addAction(QTStr("Basic.AdvAudio"), this, SLOT(OpenAdvAudio()));
	popup.addSeparator();
	popup.addMenu(&view);

	popup.exec(QCursor::pos());
}

void Soundboard::SoundboardSetMuted(bool mute)
{
	obs_source_set_muted(source, mute);
}

void Soundboard::OpenFilters()
{
	obs_frontend_open_source_filters(source);
}

void Soundboard::OpenAdvAudio()
{
	QDialog dialog(this);

	dialog.setWindowTitle(QTStr("SoundboardProps"));

	OBSAdvAudioCtrl *advAudio =
		new OBSAdvAudioCtrl(&dialog, source, usePercent);
	advAudio->adjustSize();

	dialog.exec();

	usePercent = advAudio->usePercent;
}

void Soundboard::ToggleLockVolume(bool checked)
{
	vol.data()->EnableSlider(!checked);
	lockVolume = checked;
}

void Soundboard::VolControlContextMenu()
{
	QAction lockAction(QTStr("LockVolume"));
	lockAction.setCheckable(true);
	lockAction.setChecked(lockVolume);
	QObject::connect(&lockAction, SIGNAL(toggled(bool)), this,
			 SLOT(ToggleLockVolume(bool)));

	QMenu popup(this);
	popup.addAction(&lockAction);
	popup.addSeparator();
	popup.addAction(QTStr("Filters"), this, SLOT(OpenFilters()));
	popup.addAction(QTStr("Basic.AdvAudio"), this, SLOT(OpenAdvAudio()));
	popup.exec(QCursor::pos());
}

void Soundboard::EditSoundListItem()
{
	if (editActive)
		return;

	QListWidgetItem *item = ui->soundList->currentItem();

	if (!item)
		return;

	prevName = item->text();

	Qt::ItemFlags flags = item->flags();
	SoundData *sound = FindSoundByName(item->text());

	item->setText(sound->name);
	item->setFlags(flags | Qt::ItemIsEditable);
	ui->soundList->removeItemWidget(item);
	ui->soundList->editItem(item);
	item->setFlags(flags);
	editActive = true;
}

void Soundboard::SoundListNameEdited(QWidget *editor,
				     QAbstractItemDelegate::EndEditHint endHint)
{
	QListWidgetItem *item = ui->soundList->currentItem();
	SoundData *sound = FindSoundByName(prevName);
	QLineEdit *edit = qobject_cast<QLineEdit *>(editor);
	QString name = edit->text().trimmed();

	item->setText(prevName);

	if (name == prevName)
		item->setText(prevName);
	else if (name.isEmpty())
		QMessageBox::warning(this, QTStr("EmptyName.Title"),
				     QTStr("EmptyName.Text"));
	else if (NameExists(ui->soundList, name))
		QMessageBox::warning(this, QTStr("NameExists.Title"),
				     QTStr("NameExists.Text"));
	else
		EditSound(name, sound->path);

	editActive = false;
	prevName = "";
}

QString Soundboard::GetSoundName(SoundData *sound)
{
	return sound ? sound->name : "";
}

QString Soundboard::GetSoundPath(SoundData *sound)
{
	return sound ? sound->path : "";
}

void Soundboard::SetSoundName(SoundData *sound, const QString &newName)
{
	if (sound)
		sound->name = newName;
}

void Soundboard::SetSoundPath(SoundData *sound, const QString &newPath)
{
	if (sound)
		sound->path = newPath;
}

obs_hotkey_id Soundboard::GetSoundHotkey(SoundData *sound)
{
	return sound ? sound->hotkey : OBS_INVALID_HOTKEY_ID;
}

SoundData *Soundboard::FindSoundByName(const QString &name)
{
	for (SoundData *sound : sounds) {
		if (sound->name == name)
			return sound;
	}

	return nullptr;
}

OBS_DECLARE_MODULE()
OBS_MODULE_AUTHOR("cg2121");
OBS_MODULE_USE_DEFAULT_LOCALE("Soundboard", "en-US")

bool obs_module_load(void)
{
	if (LIBOBS_API_VER < MAKE_SEMANTIC_VERSION(27, 2, 0)) {
		blog(LOG_ERROR,
		     "Soundboard plugin requires OBS 27.2.0 or newer");
		return false;
	}

	blog(LOG_INFO,
	     "Soundboard plugin version %s is loaded",
	     PLUGIN_VERSION);

	return true;
}

void obs_module_post_load(void)
{
	QMainWindow *window = (QMainWindow *)obs_frontend_get_main_window();
	obs_frontend_push_ui_translation(obs_module_get_string);

	QDockWidget *dock = new QDockWidget(window);
	dock->setObjectName("SoundboardDock");
	Soundboard *sb = new Soundboard(dock);
	dock->setWidget(sb);

	dock->setWindowTitle(QTStr("Soundboard"));
	dock->resize(400, 400);
	dock->setFloating(true);
	dock->hide();

	obs_frontend_pop_ui_translation();
}

void obs_module_unload(void) {}

MODULE_EXPORT const char *obs_module_description(void)
{
	return obs_module_text("Description");
}

MODULE_EXPORT const char *obs_module_name(void)
{
	return obs_module_text("Soundboard");
}