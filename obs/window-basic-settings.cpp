/******************************************************************************
    Copyright (C) 2013-2014 by Hugh Bailey <obs.jim@gmail.com>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
******************************************************************************/

#include <obs.hpp>
#include <util/util.hpp>
#include <util/lexer.h>
#include <sstream>
#include <QLineEdit>
#include <QMessageBox>
#include <QCloseEvent>

#include "obs-app.hpp"
#include "platform.hpp"
#include "properties-view.hpp"
#include "qt-wrappers.hpp"
#include "window-basic-main.hpp"
#include "window-basic-settings.hpp"

#include <util/platform.h>

using namespace std;

struct BaseLexer {
	lexer lex;
public:
	inline BaseLexer() {lexer_init(&lex);}
	inline ~BaseLexer() {lexer_free(&lex);}
	operator lexer*() {return &lex;}
};

/* parses "[width]x[height]", string, i.e. 1024x768 */
static bool ConvertResText(const char *res, uint32_t &cx, uint32_t &cy)
{
	BaseLexer lex;
	base_token token;

	lexer_start(lex, res);

	/* parse width */
	if (!lexer_getbasetoken(lex, &token, IGNORE_WHITESPACE))
		return false;
	if (token.type != BASETOKEN_DIGIT)
		return false;

	cx = std::stoul(token.text.array);

	/* parse 'x' */
	if (!lexer_getbasetoken(lex, &token, IGNORE_WHITESPACE))
		return false;
	if (strref_cmpi(&token.text, "x") != 0)
		return false;

	/* parse height */
	if (!lexer_getbasetoken(lex, &token, IGNORE_WHITESPACE))
		return false;
	if (token.type != BASETOKEN_DIGIT)
		return false;

	cy = std::stoul(token.text.array);

	/* shouldn't be any more tokens after this */
	if (lexer_getbasetoken(lex, &token, IGNORE_WHITESPACE))
		return false;

	return true;
}

static inline void SetComboByName(QComboBox *combo, const char *name)
{
	int idx = combo->findText(QT_UTF8(name));
	if (idx != -1)
		combo->setCurrentIndex(idx);
}

static inline void SetComboByValue(QComboBox *combo, const char *name)
{
	int idx = combo->findData(QT_UTF8(name));
	if (idx != -1)
		combo->setCurrentIndex(idx);
}

void OBSBasicSettings::HookWidget(QWidget *widget, const char *signal,
		const char *slot)
{
	QObject::connect(widget, signal, this, slot);
}

#define COMBO_CHANGED   SIGNAL(currentIndexChanged(int))
#define COMBO_CHANGED   SIGNAL(currentIndexChanged(int))
#define EDIT_CHANGED    SIGNAL(textChanged(const QString &))
#define CBEDIT_CHANGED  SIGNAL(editTextChanged(const QString &))
#define SCROLL_CHANGED  SIGNAL(valueChanged(int))

#define GENERAL_CHANGED SLOT(GeneralChanged())
#define OUTPUTS_CHANGED SLOT(OutputsChanged())
#define AUDIO_RESTART   SLOT(AudioChangedRestart())
#define AUDIO_CHANGED   SLOT(AudioChanged())
#define VIDEO_RESTART   SLOT(VideoChangedRestart())
#define VIDEO_RES       SLOT(VideoChangedResolution())
#define VIDEO_CHANGED   SLOT(VideoChanged())

OBSBasicSettings::OBSBasicSettings(QWidget *parent)
	: QDialog          (parent),
	  main             (qobject_cast<OBSBasic*>(parent)),
	  ui               (new Ui::OBSBasicSettings),
	  generalChanged   (false),
	  outputsChanged   (false),
	  audioChanged     (false),
	  videoChanged     (false),
	  pageIndex        (0),
	  loading          (true),
	  streamProperties (nullptr)
{
	string path;

	ui->setupUi(this);

	if (!GetDataFilePath("locale/locale.ini", path))
		throw "Could not find locale/locale.ini path";
	if (localeIni.Open(path.c_str(), CONFIG_OPEN_EXISTING) != 0)
		throw "Could not open locale.ini";

	HookWidget(ui->language,             COMBO_CHANGED,  GENERAL_CHANGED);
	HookWidget(ui->outputMode,           COMBO_CHANGED,  OUTPUTS_CHANGED);
	HookWidget(ui->simpleOutputPath,     EDIT_CHANGED,   OUTPUTS_CHANGED);
	HookWidget(ui->simpleOutputVBitrate, SCROLL_CHANGED, OUTPUTS_CHANGED);
	HookWidget(ui->simpleOutputABitrate, COMBO_CHANGED,  OUTPUTS_CHANGED);
	HookWidget(ui->channelSetup,         COMBO_CHANGED,  AUDIO_RESTART);
	HookWidget(ui->sampleRate,           COMBO_CHANGED,  AUDIO_RESTART);
	HookWidget(ui->desktopAudioDevice1,  COMBO_CHANGED,  AUDIO_CHANGED);
	HookWidget(ui->desktopAudioDevice2,  COMBO_CHANGED,  AUDIO_CHANGED);
	HookWidget(ui->auxAudioDevice1,      COMBO_CHANGED,  AUDIO_CHANGED);
	HookWidget(ui->auxAudioDevice2,      COMBO_CHANGED,  AUDIO_CHANGED);
	HookWidget(ui->auxAudioDevice3,      COMBO_CHANGED,  AUDIO_CHANGED);
	HookWidget(ui->renderer,             COMBO_CHANGED,  VIDEO_RESTART);
	HookWidget(ui->adapter,              COMBO_CHANGED,  VIDEO_RESTART);
	HookWidget(ui->baseResolution,       CBEDIT_CHANGED, VIDEO_RES);
	HookWidget(ui->outputResolution,     CBEDIT_CHANGED, VIDEO_RES);
	HookWidget(ui->downscaleFilter,      COMBO_CHANGED,  VIDEO_CHANGED);
	HookWidget(ui->fpsType,              COMBO_CHANGED,  VIDEO_CHANGED);
	HookWidget(ui->fpsCommon,            COMBO_CHANGED,  VIDEO_CHANGED);
	HookWidget(ui->fpsInteger,           SCROLL_CHANGED, VIDEO_CHANGED);
	HookWidget(ui->fpsInteger,           SCROLL_CHANGED, VIDEO_CHANGED);
	HookWidget(ui->fpsNumerator,         SCROLL_CHANGED, VIDEO_CHANGED);
	HookWidget(ui->fpsDenominator,       SCROLL_CHANGED, VIDEO_CHANGED);

	LoadServiceTypes();
	LoadServiceInfo();
	LoadSettings(false);
}

void OBSBasicSettings::LoadServiceTypes()
{
	const char    *type;
	size_t        idx = 0;

	while (obs_enum_service_types(idx++, &type)) {
		const char *name = obs_service_getdisplayname(type,
				App()->GetLocale());
		QString qName = QT_UTF8(name);
		QString qType = QT_UTF8(type);

		ui->streamType->addItem(qName, qType);
	}

	type = obs_service_gettype(main->GetService());
	SetComboByValue(ui->streamType, type);
}

void OBSBasicSettings::LoadServiceInfo()
{
	QLayout          *layout    = ui->streamContainer->layout();
	obs_service_t    service    = main->GetService();
	obs_data_t       settings   = obs_service_get_settings(service);
	obs_properties_t properties = obs_service_properties(service,
			App()->GetLocale());

	delete streamProperties;
	streamProperties = new OBSPropertiesView(
			settings,
			properties,
			service,
			(PropertiesUpdateCallback)obs_service_update,
			170);

	layout->addWidget(streamProperties);

	obs_data_release(settings);
}

void OBSBasicSettings::LoadLanguageList()
{
	const char *currentLang = config_get_string(GetGlobalConfig(),
			"General", "Language");

	ui->language->clear();

	size_t numSections = config_num_sections(localeIni);

	for (size_t i = 0; i < numSections; i++) {
		const char *tag = config_get_section(localeIni, i);
		const char *name = config_get_string(localeIni, tag, "Name");
		int idx = ui->language->count();

		ui->language->addItem(QT_UTF8(name), QT_UTF8(tag));

		if (strcmp(tag, currentLang) == 0)
			ui->language->setCurrentIndex(idx);
	}

	ui->language->model()->sort(0);
}

void OBSBasicSettings::LoadGeneralSettings()
{
	loading = true;

	LoadLanguageList();

	loading = false;
}

void OBSBasicSettings::LoadRendererList()
{
	const char *renderer = config_get_string(GetGlobalConfig(), "Video",
			"Renderer");

#ifdef _WIN32
	ui->renderer->addItem(QT_UTF8("Direct3D 11"));
#endif
	ui->renderer->addItem(QT_UTF8("OpenGL"));

	int idx = ui->renderer->findText(QT_UTF8(renderer));
	if (idx == -1)
		idx = 0;

	ui->renderer->setCurrentIndex(idx);
}

Q_DECLARE_METATYPE(MonitorInfo);

static string ResString(uint32_t cx, uint32_t cy)
{
	stringstream res;
	res << cx << "x" << cy;
	return res.str();
}

/* some nice default output resolution vals */
static const double vals[] =
{
	1.0,
	1.25,
	(1.0/0.75),
	1.5,
	(1.0/0.6),
	1.75,
	2.0,
	2.25,
	2.5,
	2.75,
	3.0
};

static const size_t numVals = sizeof(vals)/sizeof(double);

void OBSBasicSettings::ResetDownscales(uint32_t cx, uint32_t cy)
{
	ui->outputResolution->clear();

	for (size_t idx = 0; idx < numVals; idx++) {
		uint32_t downscaleCX = uint32_t(double(cx) / vals[idx]);
		uint32_t downscaleCY = uint32_t(double(cy) / vals[idx]);

		string res = ResString(downscaleCX, downscaleCY);
		ui->outputResolution->addItem(res.c_str());
	}

	ui->outputResolution->lineEdit()->setText(ResString(cx, cy).c_str());
}

void OBSBasicSettings::LoadResolutionLists()
{
	uint32_t cx = config_get_uint(main->Config(), "Video", "BaseCX");
	uint32_t cy = config_get_uint(main->Config(), "Video", "BaseCY");
	vector<MonitorInfo> monitors;

	ui->baseResolution->clear();

	GetMonitors(monitors);

	for (MonitorInfo &monitor : monitors) {
		string res = ResString(monitor.cx, monitor.cy);
		ui->baseResolution->addItem(res.c_str());
	}

	ResetDownscales(cx, cy);

	ui->baseResolution->lineEdit()->setText(ResString(cx, cy).c_str());

	cx = config_get_uint(main->Config(), "Video", "OutputCX");
	cy = config_get_uint(main->Config(), "Video", "OutputCY");

	ui->outputResolution->lineEdit()->setText(ResString(cx, cy).c_str());
}

static inline void LoadFPSCommon(OBSBasic *main, Ui::OBSBasicSettings *ui)
{
	const char *val = config_get_string(main->Config(), "Video",
			"FPSCommon");

	int idx = ui->fpsCommon->findText(val);
	if (idx == -1) idx = 3;
	ui->fpsCommon->setCurrentIndex(idx);
}

static inline void LoadFPSInteger(OBSBasic *main, Ui::OBSBasicSettings *ui)
{
	uint32_t val = config_get_uint(main->Config(), "Video", "FPSInt");
	ui->fpsInteger->setValue(val);
}

static inline void LoadFPSFraction(OBSBasic *main, Ui::OBSBasicSettings *ui)
{
	uint32_t num = config_get_uint(main->Config(), "Video", "FPSNum");
	uint32_t den = config_get_uint(main->Config(), "Video", "FPSDen");

	ui->fpsNumerator->setValue(num);
	ui->fpsDenominator->setValue(den);
}

void OBSBasicSettings::LoadFPSData()
{
	LoadFPSCommon(main, ui.get());
	LoadFPSInteger(main, ui.get());
	LoadFPSFraction(main, ui.get());

	uint32_t fpsType = config_get_uint(main->Config(), "Video",
			"FPSType");
	if (fpsType > 2) fpsType = 0;

	ui->fpsType->setCurrentIndex(fpsType);
	ui->fpsTypes->setCurrentIndex(fpsType);
}

void OBSBasicSettings::LoadVideoSettings()
{
	loading = true;

	if (video_output_active(obs_video())) {
		ui->videoPage->setEnabled(false);
		ui->videoMsg->setText(
				QTStr("Basic.Settings.Video.CurrentlyActive"));
	}

	LoadRendererList();
	LoadResolutionLists();
	LoadFPSData();

	loading = false;
}

void OBSBasicSettings::LoadSimpleOutputSettings()
{
	const char *path = config_get_string(main->Config(), "SimpleOutput",
			"path");
	int videoBitrate = config_get_uint(main->Config(), "SimpleOutput",
			"VBitrate");
	int audioBitrate = config_get_uint(main->Config(), "SimpleOutput",
			"ABitrate");

	ui->simpleOutputPath->setText(path);
	ui->simpleOutputVBitrate->setValue(videoBitrate);

	SetComboByName(ui->simpleOutputABitrate,
			std::to_string(audioBitrate).c_str());
}

void OBSBasicSettings::LoadOutputSettings()
{
	loading = true;

	LoadSimpleOutputSettings();

	loading = false;
}

static inline void LoadListValue(QComboBox *widget, const char *text,
		const char *val)
{
	widget->addItem(QT_UTF8(text), QT_UTF8(val));
}

void OBSBasicSettings::LoadListValues(QComboBox *widget, obs_property_t prop,
		const char *configName)
{
	size_t count = obs_property_list_item_count(prop);
	const char *deviceId = config_get_string(main->Config(), "Audio",
			configName);

	widget->addItem(QTStr("Disabled"), "disabled");

	for (size_t i = 0; i < count; i++) {
		const char *name = obs_property_list_item_name(prop, i);
		const char *val  = obs_property_list_item_string(prop, i);
		LoadListValue(widget, name, val);
	}

	int idx = widget->findData(QVariant(QT_UTF8(deviceId)));
	if (idx == -1) {
		deviceId = config_get_default_string(main->Config(), "Audio",
				configName);
		idx = widget->findData(QVariant(QT_UTF8(deviceId)));
	}

	if (idx != -1)
		widget->setCurrentIndex(idx);
}

void OBSBasicSettings::LoadAudioDevices()
{
	const char *input_id  = App()->InputAudioSource();
	const char *output_id = App()->OutputAudioSource();

	obs_properties_t input_props = obs_get_source_properties(
			OBS_SOURCE_TYPE_INPUT, input_id, App()->GetLocale());
	obs_properties_t output_props = obs_get_source_properties(
			OBS_SOURCE_TYPE_INPUT, output_id, App()->GetLocale());

	if (input_props) {
		obs_property_t inputs  = obs_properties_get(input_props,
				"device_id");
		LoadListValues(ui->auxAudioDevice1, inputs, "AuxDevice1");
		LoadListValues(ui->auxAudioDevice2, inputs, "AuxDevice2");
		LoadListValues(ui->auxAudioDevice3, inputs, "AuxDevice3");
		obs_properties_destroy(input_props);
	}

	if (output_props) {
		obs_property_t outputs = obs_properties_get(output_props,
				"device_id");
		LoadListValues(ui->desktopAudioDevice1, outputs,
				"DesktopDevice1");
		LoadListValues(ui->desktopAudioDevice2, outputs,
				"DesktopDevice2");
		obs_properties_destroy(output_props);
	}
}

void OBSBasicSettings::LoadAudioSettings()
{
	uint32_t sampleRate = config_get_uint(main->Config(), "Audio",
			"SampleRate");
	const char *speakers = config_get_string(main->Config(), "Audio",
			"ChannelSetup");

	loading = true;

	const char *str;
	if (sampleRate == 22050)
		str = "22.05khz";
	else if (sampleRate == 48000)
		str = "48khz";
	else
		str = "44.1khz";

	int sampleRateIdx = ui->sampleRate->findText(str);
	if (sampleRateIdx != -1)
		ui->sampleRate->setCurrentIndex(sampleRateIdx);

	if (strcmp(speakers, "Mono") == 0)
		ui->channelSetup->setCurrentIndex(0);
	else
		ui->channelSetup->setCurrentIndex(1);

	LoadAudioDevices();

	loading = false;
}

void OBSBasicSettings::LoadSettings(bool changedOnly)
{
	if (!changedOnly || generalChanged)
		LoadGeneralSettings();
	if (!changedOnly || outputsChanged)
		LoadOutputSettings();
	if (!changedOnly || audioChanged)
		LoadAudioSettings();
	if (!changedOnly || videoChanged)
		LoadVideoSettings();
}

void OBSBasicSettings::SaveGeneralSettings()
{
	int languageIndex = ui->language->currentIndex();
	QVariant langData = ui->language->itemData(languageIndex);
	string language = langData.toString().toStdString();

	config_set_string(GetGlobalConfig(), "General", "Language",
			language.c_str());
}

void OBSBasicSettings::SaveVideoSettings()
{
	QString renderer         = ui->renderer->currentText();
	QString baseResolution   = ui->baseResolution->currentText();
	QString outputResolution = ui->outputResolution->currentText();
	int     fpsType          = ui->fpsType->currentIndex();
	QString fpsCommon        = ui->fpsCommon->currentText();
	int     fpsInteger       = ui->fpsInteger->value();
	int     fpsNumerator     = ui->fpsNumerator->value();
	int     fpsDenominator   = ui->fpsDenominator->value();
	uint32_t cx, cy;

	/* ------------------- */

	config_set_string(GetGlobalConfig(), "Video", "Renderer",
			QT_TO_UTF8(renderer));

	if (ConvertResText(QT_TO_UTF8(baseResolution), cx, cy)) {
		config_set_uint(main->Config(), "Video", "BaseCX", cx);
		config_set_uint(main->Config(), "Video", "BaseCY", cy);
	}

	if (ConvertResText(QT_TO_UTF8(outputResolution), cx, cy)) {
		config_set_uint(main->Config(), "Video", "OutputCX", cx);
		config_set_uint(main->Config(), "Video", "OutputCY", cy);
	}

	config_set_uint(main->Config(), "Video", "FPSType", fpsType);
	config_set_string(main->Config(), "Video", "FPSCommon",
			QT_TO_UTF8(fpsCommon));
	config_set_uint(main->Config(), "Video", "FPSInt", fpsInteger);
	config_set_uint(main->Config(), "Video", "FPSNum", fpsNumerator);
	config_set_uint(main->Config(), "Video", "FPSDen", fpsDenominator);

	main->ResetVideo();
}

/* TODO: Temporary! */
void OBSBasicSettings::SaveOutputSettings()
{
	int videoBitrate     = ui->simpleOutputVBitrate->value();
	QString audioBitrate = ui->simpleOutputABitrate->currentText();
	QString path         = ui->simpleOutputPath->text();

	config_set_uint(main->Config(), "SimpleOutput", "VBitrate",
			videoBitrate);
	config_set_string(main->Config(), "SimpleOutput", "ABitrate",
			QT_TO_UTF8(audioBitrate));
	config_set_string(main->Config(), "SimpleOutput", "path",
			QT_TO_UTF8(path));
}

static inline QString GetComboData(QComboBox *combo)
{
	int idx = combo->currentIndex();
	if (idx == -1)
		return QString();

	return combo->itemData(idx).toString();
}

void OBSBasicSettings::SaveAudioSettings()
{
	QString sampleRateStr  = ui->sampleRate->currentText();
	int channelSetupIdx    = ui->channelSetup->currentIndex();
	QString desktopDevice1 = GetComboData(ui->desktopAudioDevice1);
	QString desktopDevice2 = GetComboData(ui->desktopAudioDevice2);
	QString auxDevice1     = GetComboData(ui->auxAudioDevice1);
	QString auxDevice2     = GetComboData(ui->auxAudioDevice2);
	QString auxDevice3     = GetComboData(ui->auxAudioDevice3);

	const char *channelSetup;
	if (channelSetupIdx == 0)
		channelSetup = "Mono";
	else
		channelSetup = "Stereo";

	int sampleRate = 44100;
	if (sampleRateStr == "22.05khz")
		sampleRate = 22050;
	else if (sampleRateStr == "48khz")
		sampleRate = 48000;

	config_set_uint(main->Config(), "Audio", "SampleRate", sampleRate);
	config_set_string(main->Config(), "Audio", "ChannelSetup",
			channelSetup);

	config_set_string(main->Config(), "Audio", "DesktopDevice1",
			QT_TO_UTF8(desktopDevice1));
	config_set_string(main->Config(), "Audio", "DesktopDevice2",
			QT_TO_UTF8(desktopDevice2));
	config_set_string(main->Config(), "Audio", "AuxDevice1",
			QT_TO_UTF8(auxDevice1));
	config_set_string(main->Config(), "Audio", "AuxDevice2",
			QT_TO_UTF8(auxDevice2));
	config_set_string(main->Config(), "Audio", "AuxDevice3",
			QT_TO_UTF8(auxDevice3));

	main->ResetAudioDevices();
}

void OBSBasicSettings::SaveSettings()
{
	if (generalChanged)
		SaveGeneralSettings();
	if (outputsChanged)
		SaveOutputSettings();
	if (audioChanged)
		SaveAudioSettings();
	if (videoChanged)
		SaveVideoSettings();

	config_save(main->Config());
	config_save(GetGlobalConfig());
}

bool OBSBasicSettings::QueryChanges()
{
	QMessageBox::StandardButton button;

	button = QMessageBox::question(this,
			QTStr("Basic.Settings.ConfirmTitle"),
			QTStr("Basic.Settings.Confirm"),
			QMessageBox::Yes | QMessageBox::No |
			QMessageBox::Cancel);

	if (button == QMessageBox::Cancel)
		return false;
	else if (button == QMessageBox::Yes)
		SaveSettings();
	else
		LoadSettings(true);

	ClearChanged();
	return true;
}

void OBSBasicSettings::closeEvent(QCloseEvent *event)
{
	if (Changed() && !QueryChanges())
		event->ignore();
}

void OBSBasicSettings::on_listWidget_itemSelectionChanged()
{
	int row = ui->listWidget->currentRow();

	if (loading || row == pageIndex)
		return;

	if (Changed() && !QueryChanges()) {
		ui->listWidget->setCurrentRow(pageIndex);
		return;
	}

	pageIndex = row;
}

void OBSBasicSettings::on_buttonBox_clicked(QAbstractButton *button)
{
	QDialogButtonBox::ButtonRole val = ui->buttonBox->buttonRole(button);

	if (val == QDialogButtonBox::ApplyRole ||
	    val == QDialogButtonBox::AcceptRole) {
		SaveSettings();
		ClearChanged();
	}

	if (val == QDialogButtonBox::AcceptRole ||
	    val == QDialogButtonBox::RejectRole) {
		ClearChanged();
		close();
	}
}

void OBSBasicSettings::on_streamType_currentIndexChanged(int idx)
{
	QString val = ui->streamType->itemData(idx).toString();
	obs_service_t newService;

	if (loading)
		return;

	delete streamProperties;
	streamProperties = nullptr;

	newService = obs_service_create(QT_TO_UTF8(val), nullptr, nullptr);
	if (newService)
		main->SetService(newService);

	LoadServiceInfo();
}

static inline bool StreamExists(const char *name)
{
	return obs_get_service_by_name(name) != nullptr;
}

#define INVALID_RES_STR "Basic.Settings.Video.InvalidResolution"

static bool ValidResolutions(Ui::OBSBasicSettings *ui)
{
	QString baseRes   = ui->baseResolution->lineEdit()->text();
	QString outputRes = ui->outputResolution->lineEdit()->text();
	uint32_t cx, cy;

	if (!ConvertResText(QT_TO_UTF8(baseRes), cx, cy) ||
	    !ConvertResText(QT_TO_UTF8(outputRes), cx, cy)) {

		ui->videoMsg->setText(QTStr(INVALID_RES_STR));
		return false;
	}

	ui->videoMsg->setText("");
	return true;
}

void OBSBasicSettings::on_baseResolution_editTextChanged(const QString &text)
{
	if (!loading && ValidResolutions(ui.get())) {
		QString baseResolution = text;
		uint32_t cx, cy;

		ConvertResText(QT_TO_UTF8(baseResolution), cx, cy);
		ResetDownscales(cx, cy);
	}
}

void OBSBasicSettings::GeneralChanged()
{
	if (!loading)
		generalChanged = true;
}

void OBSBasicSettings::OutputsChanged()
{
	if (!loading)
		outputsChanged = true;
}

void OBSBasicSettings::AudioChanged()
{
	if (!loading)
		audioChanged = true;
}

void OBSBasicSettings::AudioChangedRestart()
{
	if (!loading) {
		audioChanged = true;
		ui->audioMsg->setText(QTStr("Basic.Settings.ProgramRestart"));
	}
}

void OBSBasicSettings::VideoChangedRestart()
{
	if (!loading) {
		videoChanged = true;
		ui->videoMsg->setText(QTStr("Basic.Settings.ProgramRestart"));
	}
}

void OBSBasicSettings::VideoChangedResolution()
{
	if (!loading && ValidResolutions(ui.get()))
		videoChanged = true;
}

void OBSBasicSettings::VideoChanged()
{
	if (!loading)
		videoChanged = true;
}
