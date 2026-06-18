#include "plugin.hpp"

struct IdmGlitchNode : Module {
	enum ParamId {
		PROB_PARAM,
		GLITCH_DENSITY_PARAM,
		MANUAL_INJECT_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		CLOCK_IN,
		RESET_IN,
		PROB_CV_IN,
		INPUTS_LEN
	};
	enum OutputId {
		GATE_OUT,
		PITCH_CV_OUT,
		ACCENT_OUT,
		OUTPUTS_LEN
	};
	enum LightId {
		GATE_LIGHT,
		ACCENT_LIGHT,
		LIGHTS_LEN
	};

	dsp::SchmittTrigger clockTrigger;
	dsp::SchmittTrigger resetTrigger;
	dsp::SchmittTrigger injectTrigger;
	dsp::PulseGenerator gatePulse;
	dsp::PulseGenerator accentPulse;

	uint16_t shiftRegister = 0x5A5A;
	float currentCv = 0.f;
	
	float glitchBlinkTimer = 0.f;
	bool isGlitching = false;

	IdmGlitchNode() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configParam(PROB_PARAM, 0.f, 1.f, 0.1f, "Mutation Probability");
		configParam(GLITCH_DENSITY_PARAM, 0.f, 1.f, 0.2f, "Glitch Density");
		configParam(MANUAL_INJECT_PARAM, 0.f, 1.f, 0.f, "Manual Inject");
		
		configInput(CLOCK_IN, "Clock");
		configInput(RESET_IN, "Reset");
		configInput(PROB_CV_IN, "Probability CV");
		
		configOutput(GATE_OUT, "Gate");
		configOutput(PITCH_CV_OUT, "Pitch CV");
		configOutput(ACCENT_OUT, "Accent");
	}

	void process(const ProcessArgs& args) override {
		if (resetTrigger.process(inputs[RESET_IN].getVoltage())) {
			shiftRegister = 0x5A5A;
		}

		if (injectTrigger.process(params[MANUAL_INJECT_PARAM].getValue())) {
			shiftRegister = (shiftRegister >> 1) | 0x8000;
			// Trigger immediate feedback
			gatePulse.trigger(1e-3f);
			accentPulse.trigger(1e-3f);
			glitchBlinkTimer = 0.1f;
			
			// Recalculate CV immediately
			const uint8_t cvBits = static_cast<uint8_t>(shiftRegister & 0xFF);
			float rawCv = (cvBits / 255.f) * 10.f - 5.f;
			currentCv = std::round(rawCv * 12.f) / 12.f;
		}

		const bool clockEdge = clockTrigger.process(inputs[CLOCK_IN].getVoltage());

		if (clockEdge) {
			float prob = params[PROB_PARAM].getValue();
			if (inputs[PROB_CV_IN].isConnected()) {
				prob += inputs[PROB_CV_IN].getVoltage() / 10.f;
			}
			prob = clamp(prob, 0.f, 1.f);

			const bool lastBit = (shiftRegister & 1) != 0;
			const bool flip = random::uniform() < prob;
			const bool newBit = lastBit ^ flip;

			shiftRegister = (shiftRegister >> 1) | (static_cast<uint16_t>(newBit) << 15);

			const uint8_t cvBits = static_cast<uint8_t>(shiftRegister & 0xFF);
			float rawCv = (cvBits / 255.f) * 10.f - 5.f;
			currentCv = std::round(rawCv * 12.f) / 12.f;

			if (shiftRegister & 1) {
				gatePulse.trigger(1e-3f);
				
				float density = params[GLITCH_DENSITY_PARAM].getValue();
				if (random::uniform() < density) {
					accentPulse.trigger(1e-3f);
					glitchBlinkTimer = 0.1f;
				}
			}
		}

		if (glitchBlinkTimer > 0.f) {
			glitchBlinkTimer -= args.sampleTime;
			isGlitching = true;
		} else {
			isGlitching = false;
		}

		const float gateV = gatePulse.process(args.sampleTime) ? 10.f : 0.f;
		const float accentV = accentPulse.process(args.sampleTime) ? 10.f : 0.f;

		outputs[GATE_OUT].setVoltage(gateV);
		outputs[PITCH_CV_OUT].setVoltage(clamp(currentCv, -5.f, 5.f));
		outputs[ACCENT_OUT].setVoltage(accentV);

		// Drive LEDs based on outputs using Smooth for realistic hardware decay
		lights[GATE_LIGHT].setBrightnessSmooth(gateV / 10.f, args.sampleTime);
		lights[ACCENT_LIGHT].setBrightnessSmooth(accentV / 10.f, args.sampleTime);
	}
};

struct GlitchDisplay : TransparentWidget {
	IdmGlitchNode* module;
	GlitchDisplay() {}

	void draw(const DrawArgs& args) override {
		if (!module) return;

		// 1. Draw a dark screen background
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, 0, 0, box.size.x, box.size.y, 4.0f);
		nvgFillColor(args.vg, nvgRGBA(15, 15, 20, 255));
		nvgFill(args.vg);

		// 2. Get Params
		float prob = module->params[IdmGlitchNode::PROB_PARAM].getValue();
		if (module->inputs[IdmGlitchNode::PROB_CV_IN].isConnected()) {
			prob += module->inputs[IdmGlitchNode::PROB_CV_IN].getVoltage() / 10.f;
		}
		prob = clamp(prob, 0.f, 1.f);
		float dens = module->params[IdmGlitchNode::GLITCH_DENSITY_PARAM].getValue();

		float cx = box.size.x / 2.0f;
		float cy = box.size.y / 2.0f;

		// Draw animated arcs for PROB and DENS
		nvgBeginPath(args.vg);
		nvgArc(args.vg, 20, cy, 12.0f, M_PI * 0.5f, M_PI * 0.5f + prob * M_PI * 2.0f, NVG_CW);
		nvgStrokeColor(args.vg, nvgRGBA(0, 255, 150, 150));
		nvgStrokeWidth(args.vg, 2.0f);
		nvgStroke(args.vg);

		nvgBeginPath(args.vg);
		nvgArc(args.vg, box.size.x - 20, cy, 12.0f, M_PI * 0.5f, M_PI * 0.5f + dens * M_PI * 2.0f, NVG_CW);
		nvgStrokeColor(args.vg, nvgRGBA(255, 100, 100, 150));
		nvgStrokeWidth(args.vg, 2.0f);
		nvgStroke(args.vg);

		// Center - 16 bit ring representation of the shift register
		float ringRadius = 14.0f;
		for (int i = 0; i < 16; i++) {
			bool bit = (module->shiftRegister >> i) & 1;
			float angle = i * (M_PI * 2.0f / 16.0f) - (M_PI / 2.0f);
			float rx = cx + cos(angle) * ringRadius;
			float ry = cy + sin(angle) * ringRadius;
			
			nvgBeginPath(args.vg);
			nvgCircle(args.vg, rx, ry, 2.0f);
			if (bit) {
				nvgFillColor(args.vg, nvgRGBA(0, 200, 255, 255));
			} else {
				nvgFillColor(args.vg, nvgRGBA(40, 50, 60, 255));
			}
			nvgFill(args.vg);
		}

		// Glitch flash indicators on edges
		if (module->isGlitching) {
			nvgBeginPath(args.vg);
			nvgRect(args.vg, 0, 0, 4, box.size.y);
			nvgRect(args.vg, box.size.x - 4, 0, 4, box.size.y);
			nvgFillColor(args.vg, nvgRGBA(255, 50, 50, 200));
			nvgFill(args.vg);
		}
	}
};

struct PanelTextDisplay : TransparentWidget {
	std::shared_ptr<Font> font;

	PanelTextDisplay() {
		font = APP->window->loadFont(asset::plugin(pluginInstance, "res/fonts/Roboto-Bold.ttf"));
	}

	void draw(const DrawArgs& args) override {
		if (!font) return;
		nvgFontFaceId(args.vg, font->handle);

		// 0. Module Name at the top (Fat & Flat IDM style)
		nvgFontSize(args.vg, 12);
		nvgFillColor(args.vg, nvgRGBA(240, 240, 240, 255));
		nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		
		nvgSave(args.vg);
		nvgTranslate(args.vg, mm2px(40.64), mm2px(6.0f));
		nvgScale(args.vg, 1.4f, 0.85f); // Scale X up to make it wide, Y down to make it flat
		nvgTextLetterSpacing(args.vg, 1.5f);
		nvgText(args.vg, 0, 0, "IDM GLITCH NODE", NULL);
		nvgRestore(args.vg);

		// 1. Draw knob/port labels
		nvgFontSize(args.vg, 11);
		nvgFillColor(args.vg, nvgRGBA(200, 200, 200, 255)); // Light grey (grey-white)
		nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_BOTTOM);

		// Y=60mm (Knobs) - Moved labels to below the knobs
		nvgText(args.vg, mm2px(20.64), mm2px(76.0f), "PROB", NULL);
		nvgText(args.vg, mm2px(60.64), mm2px(76.0f), "DENS", NULL);

		// Y=85mm (Inputs)
		nvgText(args.vg, mm2px(20.64), mm2px(94.5), "CLK", NULL);
		nvgText(args.vg, mm2px(40.64), mm2px(94.5), "RST", NULL);
		nvgText(args.vg, mm2px(60.64), mm2px(94.5), "CV", NULL);

		// Y=105mm (Outputs & Inject Button)
		nvgText(args.vg, mm2px(16.64), mm2px(114.5), "INJ", NULL);
		nvgText(args.vg, mm2px(32.64), mm2px(114.5), "GATE", NULL);
		nvgText(args.vg, mm2px(48.64), mm2px(114.5), "PITCH", NULL);
		nvgText(args.vg, mm2px(64.64), mm2px(114.5), "ACC", NULL);

		// 2. Brand Name at the bottom
		nvgFontSize(args.vg, 12);
		nvgFillColor(args.vg, nvgRGBA(240, 240, 240, 255));
		nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		
		nvgSave(args.vg);
		nvgTranslate(args.vg, mm2px(40.64), mm2px(122.5f));
		nvgScale(args.vg, 1.4f, 0.85f);
		nvgTextLetterSpacing(args.vg, 1.5f);
		nvgText(args.vg, 0, 0, "AERODYNE", NULL);
		nvgRestore(args.vg);
	}
};

struct ScrewStar : app::SvgScrew {
	ScrewStar() {
		setSvg(APP->window->loadSvg(asset::plugin(pluginInstance, "res/ScrewStar.svg")));
	}
};

struct IdmGlitchNodeWidget : ModuleWidget {
	IdmGlitchNodeWidget(IdmGlitchNode* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/IdmGlitchNode.svg")));

		// Add 4 star screws to match the panel color
		addChild(createWidget<ScrewStar>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewStar>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewStar>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewStar>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		// Screen Size: W=75.28mm, H=40.0mm. Centered on 81.28mm panel (16HP)
		GlitchDisplay* display = new GlitchDisplay();
		display->module = module;
		display->box.pos = mm2px(Vec(3.0, 11.0)); // Kept at Y=11.0
		display->box.size = mm2px(Vec(75.28, 36.0)); // Shrunk height from 40 to 36 to avoid overlapping large knobs
		addChild(display);

		// Labels display
		PanelTextDisplay* textDisplay = new PanelTextDisplay();
		textDisplay->box.pos = Vec(0, 0);
		textDisplay->box.size = mm2px(Vec(81.28, 128.5));
		addChild(textDisplay);

		// Row 1: Knobs (Y = 60.0 mm) - 左右对称
		addParam(createParamCentered<RoundHugeBlackKnob>(mm2px(Vec(20.64, 60.0)), module, IdmGlitchNode::PROB_PARAM));
		addParam(createParamCentered<RoundHugeBlackKnob>(mm2px(Vec(60.64, 60.0)), module, IdmGlitchNode::GLITCH_DENSITY_PARAM));

		// Row 2: Inputs (Y = 85.0 mm) - 左中右对称
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(20.64, 85.0)), module, IdmGlitchNode::CLOCK_IN));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(40.64, 85.0)), module, IdmGlitchNode::RESET_IN));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(60.64, 85.0)), module, IdmGlitchNode::PROB_CV_IN));

		// Row 3: Outputs & Inject Button (Y = 105.0 mm) - 紧凑排列
		addParam(createParamCentered<BefacoPush>(mm2px(Vec(16.64, 105.0)), module, IdmGlitchNode::MANUAL_INJECT_PARAM));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(32.64, 105.0)), module, IdmGlitchNode::GATE_OUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(48.64, 105.0)), module, IdmGlitchNode::PITCH_CV_OUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(64.64, 105.0)), module, IdmGlitchNode::ACCENT_OUT));

		// LEDs (Above and to the right of GATE and ACC ports)
		addChild(createLightCentered<SmallLight<GreenLight>>(mm2px(Vec(32.64 + 4.5, 105.0 - 5.0)), module, IdmGlitchNode::GATE_LIGHT));
		addChild(createLightCentered<SmallLight<RedLight>>(mm2px(Vec(64.64 + 4.5, 105.0 - 5.0)), module, IdmGlitchNode::ACCENT_LIGHT));
	}
};

Model* modelIdmGlitchNode = createModel<IdmGlitchNode, IdmGlitchNodeWidget>("IdmGlitchNode");
