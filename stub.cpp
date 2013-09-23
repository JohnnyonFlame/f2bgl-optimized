/*
 * Fade To Black engine rewrite
 * Copyright (C) 2006-2012 Gregory Montoir (cyx@users.sourceforge.net)
 */

#include "file.h"
#include "game.h"
#include "mixer.h"
#include "sound.h"
#include "render.h"
#include "stub.h"

static const char *USAGE =
	"Fade2Black/OpenGL\n"
	"Usage: f2b [OPTIONS]...\n"
	"  --datapath=PATH             Path to data files (default 'DATA')\n"
	"  --language=EN|FR|GR|SP|IT   Language files to use (default 'EN')\n"
	"  --playdemo                  Use inputs from .DEM files\n"
	"  --level=NUM                 Start at level NUM\n";

static bool parseOptionStr(const char *arg, const char *name, const char **opt) {
	bool handled = false;
	if (arg[0] == '-' && arg[1] == '-') {
		arg += 2;
		if (strncmp(arg, name, strlen(name)) == 0) {
			if (opt) {
				*opt = arg + strlen(name);
			}
			handled = true;
		}
	}
	return handled;
}

static bool parseOptionBool(const char *arg, const char *name, bool *ret) {
	*ret = parseOptionStr(arg, name, 0);
	return *ret;
}

static bool parseOptionInt(const char *arg, const char *name, int *i) {
	const char *opt;
	if (parseOptionStr(arg, name, &opt)) {
		*i = strtol(opt, 0, 0);
		return true;
	}
	return false;
}

static const struct {
	FileLanguage lang;
	const char *str;
	bool voice;
} _languages[] = {
	{ kFileLanguage_EN, "EN", true  },
	{ kFileLanguage_FR, "FR", true  },
	{ kFileLanguage_GR, "GR", true  },
	{ kFileLanguage_SP, "SP", false },
	{ kFileLanguage_IT, "IT", false }
};

static FileLanguage parseLanguage(const char *language) {
	for (int i = 0; i < ARRAYSIZE(_languages); ++i) {
		if (strcasecmp(_languages[i].str, language) == 0) {
			return _languages[i].lang;
		}
	}
	return _languages[0].lang;
}

static FileLanguage parseVoice(const char *voice, FileLanguage lang) {
	switch (lang) {
	case kFileLanguage_SP:
	case kFileLanguage_IT:
		for (int i = 0; i < ARRAYSIZE(_languages); ++i) {
			if (strcasecmp(_languages[i].str, voice) == 0) {
				if (_languages[i].voice) {
					return _languages[i].lang;
				}
				break;
			}
		}
		// default to English
		return kFileLanguage_EN;
	default:
		// voice must match text for other languages
		return lang;
	}
}

static int getNextIntroCutsceneNum(int num) {
	switch (num) {
	case 47:
		return 39;
	case 39:
		return 13;
	case 13:
		return 37;
	case 37:
		return 53;
	case 53:
		return 29;
	}
	return -1;
}

static const char *_dataPath = ".";
static int _skipCutscenes;

struct GameStub_F2B : GameStub {

	Render *_render;
	Game *_g;
	int _state, _nextState;
	int _dt;
	bool _skip;

	void setState(int state) {
		printf("stub.state %d\n", state);
		// release
		if (_state == 0) {
			_render->setPalette(_g->_screenPalette, 256);
		}
		// init
		if (state == 0) {
			if (_skipCutscenes) {
				warning("Skipping cutscene %d playback", _g->_cut._numToPlay);
				_g->_cut._numToPlay = -1;
				return;
			}
			_g->_cut.load(_g->_cut._numToPlay);
		}
		if (state == 1) {
			_render->setOverlayDim(0, 0);
			_g->updatePalette();
		}
		if (state == 2) {
			_render->setOverlayDim(320, 200);
			if (!_g->initInventory()) {
				return;
			}
		}
		if (state == 3) {
			_render->setOverlayDim(320, 200);
			_g->initBox();
		}
		if (state == 4) {
			_g->initMenu();
		}
		_state = state;
	}

	virtual int init(int argc, char *argv[]) {
		GameParams params;
		const char *language = "";
		const char *voice = "";
		for (int i = 0; i < argc; ++i) {
			bool opt = false;
			if (strlen(argv[i]) >= 2) {
				opt |= parseOptionStr(argv[i], "datapath=", &_dataPath);
				opt |= parseOptionStr(argv[i], "language=", &language);
				opt |= parseOptionBool(argv[i], "playdemo", &params.playDemo);
				opt |= parseOptionInt(argv[i], "level=", &params.levelNum);
				opt |= parseOptionStr(argv[i], "voice=", &voice);
#ifdef F2B_DEBUG
				opt |= parseOptionInt(argv[i], "xpos_conrad=", &params.xPosConrad);
				opt |= parseOptionInt(argv[i], "zpos_conrad=", &params.zPosConrad);
				opt |= parseOptionInt(argv[i], "skip_cutscenes=", &_skipCutscenes);
#endif
			}
			if (!opt) {
				printf("%s\n", USAGE);
				return -1;
			}
		}
		g_utilDebugMask = kDebug_INFO;
#ifdef F2B_DEBUG
//		g_utilDebugMask |= kDebug_GAME | kDebug_OPCODES | kDebug_SOUND | kDebug_RESOURCE | kDebug_FILE;
		_skipCutscenes = 1;
#endif
		FileLanguage fileLanguage = parseLanguage(language);
		FileLanguage fileVoice = parseVoice(voice, fileLanguage);
		if (!fileInit(fileLanguage, fileVoice, _dataPath)) {
			warning("Unable to find datafiles in '%s'", _dataPath);
			return -1;
		}
		_render = new Render;
		_g = new Game(_render, &params);
		_g->init();
		_g->_cut._numToPlay = 47;
		_state = -1;
		_nextState = _skipCutscenes ? 1 : 0;
		setState(_nextState);
		_nextState = _state;
		_dt = 0;
		_skip = false;
		return 0;
	}
	virtual void quit() {
		delete _g;
		delete _render;
	}
	virtual StubMixProc getMixProc(int rate, int fmt, void (*lock)(int)) {
		StubMixProc mix;
		mix.proc = Mixer::mixCb;
		mix.data = &_g->_snd._mix;
		_g->_snd._mix.setFormat(rate, fmt);
		_g->_snd._mix._lock = lock;
		return mix;
	}
	virtual void queueKeyInput(int keycode, int pressed) {
		switch (keycode) {
		case kKeyCodeLeft:
			if (pressed) _g->inp.dirMask |= kInputDirLeft;
			else _g->inp.dirMask &= ~kInputDirLeft;
			break;
		case kKeyCodeRight:
			if (pressed) _g->inp.dirMask |= kInputDirRight;
			else _g->inp.dirMask &= ~kInputDirRight;
			break;
		case kKeyCodeUp:
			if (pressed) _g->inp.dirMask |= kInputDirUp;
			else _g->inp.dirMask &= ~kInputDirUp;
			break;
		case kKeyCodeDown:
			if (pressed) _g->inp.dirMask |= kInputDirDown;
			else _g->inp.dirMask &= ~kInputDirDown;
			break;
		case kKeyCodeAlt:
			_g->inp.altKey = pressed;
			break;
		case kKeyCodeShift:
			_g->inp.shiftKey = pressed;
			break;
		case kKeyCodeCtrl:
			_g->inp.ctrlKey = pressed;
			break;
		case kKeyCodeSpace:
			_g->inp.spaceKey = pressed;
			break;
		case kKeyCodeTab:
			_g->inp.tabKey = pressed;
			break;
		case kKeyCodeEscape:
			_g->inp.escapeKey = pressed;
			break;
		case kKeyCodeH:
			_g->inp.cameraKey = 1;
			break;
		case kKeyCodeI:
			_g->inp.inventoryKey = pressed;
			break;
		case kKeyCodeJ:
			_g->inp.jumpKey = pressed;
			break;
		case kKeyCodeK:
			_g->inp.cameraKey = 2;
			break;
		case kKeyCodeU:
			_g->inp.useKey = pressed;
			break;
		case kKeyCodeReturn:
			_g->inp.enterKey = pressed;
			break;
		case kKeyCode1:
		case kKeyCode2:
		case kKeyCode3:
		case kKeyCode4:
		case kKeyCode5:
			_g->inp.numKeys[1 + keycode - kKeyCode1] = pressed;
			break;
		}
	}
	bool syncTicks(unsigned int ticks, int tickDuration) {
		static int previousTicks = ticks;
		_dt += ticks - previousTicks;
		const bool ret = (_dt < tickDuration);
		previousTicks = ticks;
		while (_dt >= tickDuration) {
			_dt -= tickDuration;
		}
		return ret;
	}
	virtual void doTick(unsigned int ticks) {
		if (_nextState != _state) {
			setState(_nextState);
		}
		_nextState = _state;
		switch (_state) {
		case 0: // cutscene
			_render->clearScreen();
			_skip = syncTicks(ticks, kCutsceneFrameDelay);
			if (_skip) {
				return;
			}
			if (!_g->_cut.play()) {
				_g->_cut.unload();
				if (!_g->_cut.isInterrupted()) {
					do {
						_g->_cut._numToPlay = getNextIntroCutsceneNum(_g->_cut._numToPlay);
					} while (_g->_cut._numToPlay >= 0 && !_g->_cut.load(_g->_cut._numToPlay));
				} else {
					_g->_cut._numToPlay = -1;
				}
				if (_g->_cut._numToPlay < 0) {
					_nextState = 1;
				}
			}
			/*_g->_cut._numToPlay = -1;
			_nextState = 1;*/
			break;
		case 1: // game
			if (_g->_changeLevel) {
				_g->_changeLevel = false;
				warning("_changeLevel flag set, starting level %d", _g->_level);
				_g->initLevel();
			} else if (_g->_endGame) {
				_g->_endGame = false;
				warning("_endGame flag set, starting level %d", _g->_level);
				_g->initLevel();
			}
			_g->updateGameInput();
			_g->doTick();
			if (_g->inp.inventoryKey) {
				_g->inp.inventoryKey = false;
				_nextState = 2;
			} else if (_g->inp.escapeKey) {
				_g->inp.escapeKey = false;
				_nextState = 4;
			} else if (_g->_cut._numToPlay >= 0) {
				_nextState = 0;
			} else if (_g->_boxItemCount != 0) {
				_nextState = 3;
			}
			break;
		case 2: // inventory
			_g->updateInventoryInput();
			_g->doInventory();
			if (_g->inp.inventoryKey) {
				_g->inp.inventoryKey = false;
				_g->closeInventory();
				_nextState = 1;
			}
			break;
		case 3: // box
			_g->doBox();
			if (_g->_boxItemCount == 0) {
				_nextState = 1;
			}
			break;
		case 4: // menu
			_g->doMenu();
			if (_g->inp.escapeKey) {
				_g->inp.escapeKey = false;
				_nextState = 1;
			}
			break;
		}
	}
	virtual void initGL(int w, int h) {
		_render->resizeScreen(w, h);
	}
	virtual void drawGL() {
		_render->drawOverlay();
	}
};

extern "C" {
	GameStub *GameStub_create() {
		return new GameStub_F2B;
	}
};
