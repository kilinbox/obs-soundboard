#pragma once

#include "obs.hpp"
#include <QString>
#include <vector>

class SoundData {
public:
	static std::vector<SoundData *> sounds;
	QString name = "";
	QString path = "";
	obs_hotkey_id hotkey = OBS_INVALID_HOTKEY_ID;
	bool loop = false;

	SoundData(const QString &name_, const QString &path_);
	~SoundData();

	static void SetName(SoundData &sound, const QString &newName);
	static void SetPath(SoundData &sound, const QString &newPath);

	static QString GetName(const SoundData &sound);
	static QString GetPath(const SoundData &sound);
	static obs_hotkey_id GetHotkey(const SoundData &sound);
	static SoundData *FindSoundByName(const QString &name);

	static void SetLoopingEnabled(SoundData &sound, bool loop);
	static bool LoopingEnabled(const SoundData &sound);
};
