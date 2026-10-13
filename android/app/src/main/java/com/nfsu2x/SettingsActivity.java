package com.nfsu2x;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.CompoundButton;
import android.widget.LinearLayout;
import android.widget.RadioButton;
import android.widget.RadioGroup;
import android.widget.ScrollView;
import android.widget.TextView;

/** Frame rate, screen stretch, render resolution and the control editor. */
public class SettingsActivity extends Activity {

    private static final int BG = 0xFF101418, FG = 0xFFFFFFFF, DIM = 0xFF99A0A8;

    @Override
    protected void onCreate(Bundle b) {
        super.onCreate(b);
        float d = getResources().getDisplayMetrics().density;
        int pad = (int) (20 * d);

        LinearLayout col = new LinearLayout(this);
        col.setOrientation(LinearLayout.VERTICAL);
        col.setPadding(pad, pad, pad, pad);

        col.addView(label(getString(R.string.settings), 22, FG, true));

        // ---- frame rate ----
        col.addView(label(getString(R.string.fps_title), 17, FG, true));
        col.addView(label(getString(R.string.fps_note), 13, DIM, false));
        RadioGroup fps = new RadioGroup(this);
        String[] fpsText = { getString(R.string.fps_30), getString(R.string.fps_60),
                getString(R.string.fps_120) };
        for (int i = 0; i < Settings.FPS.length; i++) {
            RadioButton r = radio(fpsText[i], i);
            fps.addView(r);
            if (Settings.FPS[i] == Settings.fps(this))
                r.setChecked(true);
        }
        fps.setOnCheckedChangeListener(new RadioGroup.OnCheckedChangeListener() {
            @Override public void onCheckedChanged(RadioGroup g, int id) {
                Settings.setFps(SettingsActivity.this, Settings.FPS[id]);
            }
        });
        col.addView(fps);

        // ---- stretch ----
        col.addView(label(getString(R.string.stretch_title), 17, FG, true));
        CheckBox stretch = new CheckBox(this);
        stretch.setText(R.string.stretch_note);
        stretch.setTextColor(0xFFDDDDDD);
        stretch.setChecked(Settings.stretch(this));
        stretch.setOnCheckedChangeListener(new CompoundButton.OnCheckedChangeListener() {
            @Override public void onCheckedChanged(CompoundButton v, boolean on) {
                Settings.setStretch(SettingsActivity.this, on);
            }
        });
        col.addView(stretch);

        // ---- resolution ----
        col.addView(label(getString(R.string.res_title), 17, FG, true));
        col.addView(label(getString(R.string.res_note), 13, DIM, false));
        RadioGroup res = new RadioGroup(this);
        for (int i = 0; i < Settings.SCALES.length; i++) {
            RadioButton r = radio(Settings.scaleLabel(i), i);
            res.addView(r);
            if (i == Settings.scaleIndex(this))
                r.setChecked(true);
        }
        res.setOnCheckedChangeListener(new RadioGroup.OnCheckedChangeListener() {
            @Override public void onCheckedChanged(RadioGroup g, int id) {
                Settings.setScaleIndex(SettingsActivity.this, id);
            }
        });
        col.addView(res);

        // ---- controls ----
        col.addView(label(getString(R.string.controls_title), 17, FG, true));
        Button edit = new Button(this);
        edit.setText(R.string.edit_controls);
        edit.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) {
                startActivity(new Intent(SettingsActivity.this, ControlEditorActivity.class));
            }
        });
        col.addView(edit);

        Button back = new Button(this);
        back.setText(R.string.back);
        back.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) { finish(); }
        });
        col.addView(back);

        ScrollView sv = new ScrollView(this);
        sv.setBackgroundColor(BG);
        sv.addView(col);
        setContentView(sv);
    }

    private TextView label(String s, int sp, int color, boolean topGap) {
        TextView t = new TextView(this);
        t.setText(s);
        t.setTextSize(sp);
        t.setTextColor(color);
        t.setGravity(Gravity.START);
        t.setPadding(0, topGap ? (int) (14 * getResources().getDisplayMetrics().density) : 0, 0, 6);
        return t;
    }

    private RadioButton radio(String text, int id) {
        RadioButton r = new RadioButton(this);
        r.setId(id);
        r.setText(text);
        r.setTextColor(0xFFDDDDDD);
        return r;
    }
}
