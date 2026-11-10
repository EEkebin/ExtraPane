package io.github.eekebin.extrapane;

import android.content.Context;
import android.graphics.*;
import android.graphics.drawable.Drawable;
import android.graphics.drawable.GradientDrawable;
import android.widget.Button;

/** One accessible resource action, with the same power glyph in either state. */
final class PowerButton extends Button {
    private final String resource;

    PowerButton(Context context, String resource, Runnable action) {
        super(context);
        this.resource = resource;
        setAllCaps(false);
        setTextColor(Color.WHITE);
        setMinimumHeight(dp(48));
        setPadding(dp(14), dp(8), dp(14), dp(8));
        setCompoundDrawablePadding(dp(8));
        Drawable icon =
                new Drawable() {
                    final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);

                    public void draw(Canvas canvas) {
                        Rect bounds = getBounds();
                        canvas.save();
                        canvas.translate(bounds.left, bounds.top);
                        canvas.scale(bounds.width() / 24f, bounds.height() / 24f);
                        paint.setColor(Color.WHITE);
                        paint.setStyle(Paint.Style.STROKE);
                        paint.setStrokeWidth(2.2f);
                        paint.setStrokeCap(Paint.Cap.ROUND);
                        canvas.drawArc(4, 4, 20, 20, -45, 270, false, paint);
                        canvas.drawLine(12, 2, 12, 12, paint);
                        canvas.restore();
                    }

                    public void setAlpha(int alpha) {}

                    public void setColorFilter(ColorFilter filter) {}

                    public int getOpacity() {
                        return PixelFormat.TRANSLUCENT;
                    }
                };
        icon.setBounds(0, 0, dp(22), dp(22));
        setCompoundDrawablesRelative(icon, null, null, null);
        setOnClickListener(v -> action.run());
        state(false, false);
    }

    private int dp(int value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }

    void state(boolean active, boolean stopping) {
        String action = stopping ? "Stopping " : active ? "Disconnect " : "Connect ";
        String description = action + resource.toLowerCase(java.util.Locale.ROOT);
        setText(description);
        setContentDescription(description);
        setTooltipText(description);
        setEnabled(!stopping);
        setAlpha(stopping ? .55f : 1f);
        GradientDrawable background = new GradientDrawable();
        background.setCornerRadius(dp(10));
        background.setColor(active ? Color.rgb(179, 38, 53) : Color.rgb(29, 116, 74));
        setBackgroundTintList(null);
        setBackground(background);
    }
}
