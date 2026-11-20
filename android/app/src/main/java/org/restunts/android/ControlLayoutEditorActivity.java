package org.restunts.android;

import android.app.Activity;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Insets;
import android.graphics.Paint;
import android.os.Build;
import android.os.Bundle;
import android.util.Log;
import android.view.Gravity;
import android.view.MotionEvent;
import android.view.View;
import android.view.Window;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.SeekBar;
import android.widget.Toast;

/** Edit a draft of the touch controls using the same usable area as the game. */
public final class ControlLayoutEditorActivity extends Activity {
    private static final float SLIDER_WIDTH = 0.30f;
    private static final int SLIDER_STEPS = 200;
    private static final int ACTION_SIZE_DP = 52;
    private static final int ACTION_TOP_MARGIN_DP = 8;
    // Leave space to grab the camera circle at its default position.
    private static final int ACTION_RIGHT_MARGIN_DP = 32;
    private static final int ACTION_TEXT_SP = 26;
    private static final int GREEN = Color.rgb(106, 226, 137);
    private static final int RED = Color.rgb(255, 129, 129);
    private static final int BACKGROUND = Color.rgb(23, 26, 30);
    private ControlLayout draft;

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        try {
            ControlLayout.loadLibraries();
        } catch (LinkageError error) {
            Log.e("ControlLayoutEditor", "Could not load the game libraries", error);
            Toast.makeText(this, "Could not open the control layout editor.", Toast.LENGTH_LONG).show();
            finish();
            return;
        }
        draft = ControlLayout.restore(this, state);

        configureFullscreen();

        CircleView circles = new CircleView();
        SeekBar slider = new SeekBar(this);
        slider.setContentDescription("Control circle size");
        slider.setLayoutDirection(View.LAYOUT_DIRECTION_LTR);
        slider.setMax(SLIDER_STEPS);
        slider.setProgress(Math.round(SLIDER_STEPS * (draft.scale - ControlLayout.MIN_SCALE)
            / (ControlLayout.MAX_SCALE - ControlLayout.MIN_SCALE)));
        slider.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override
            public void onProgressChanged(SeekBar bar, int progress, boolean fromUser) {
                draft.scale = ControlLayout.MIN_SCALE + (ControlLayout.MAX_SCALE - ControlLayout.MIN_SCALE)
                    * progress / SLIDER_STEPS;
                circles.refreshCircles();
            }

            @Override
            public void onStartTrackingTouch(SeekBar bar) {}

            @Override
            public void onStopTrackingTouch(SeekBar bar) {}
        });

        FrameLayout page = new FrameLayout(this) {
            @Override
            protected void onMeasure(int widthSpec, int heightSpec) {
                slider.getLayoutParams().width = Math.round(MeasureSpec.getSize(widthSpec) * SLIDER_WIDTH);
                super.onMeasure(widthSpec, heightSpec);
            }
        };
        page.setBackgroundColor(BACKGROUND);
        page.addView(circles, new FrameLayout.LayoutParams(
            FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT));
        page.addView(slider, new FrameLayout.LayoutParams(
            0, FrameLayout.LayoutParams.WRAP_CONTENT, Gravity.CENTER));
        LinearLayout actions = new LinearLayout(this);
        addAction(actions, "\u2713", "Save control layout", GREEN, () -> {
            draft.save(this);
            finish();
        });
        addAction(actions, "\u2715", "Discard control layout changes", RED, this::finish);
        FrameLayout.LayoutParams actionsPosition = new FrameLayout.LayoutParams(
            FrameLayout.LayoutParams.WRAP_CONTENT, FrameLayout.LayoutParams.WRAP_CONTENT,
            Gravity.TOP | Gravity.RIGHT);
        int topMargin = dp(ACTION_TOP_MARGIN_DP);
        int rightMargin = dp(ACTION_RIGHT_MARGIN_DP);
        actionsPosition.topMargin = topMargin;
        actionsPosition.rightMargin = rightMargin;
        page.addView(actions, actionsPosition);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            page.setOnApplyWindowInsetsListener((view, insets) -> {
                // Include every inset the SDL surface uses to calculate the game's safe area.
                Insets safe = insets.getInsets(WindowInsets.Type.systemBars()
                    | WindowInsets.Type.systemGestures() | WindowInsets.Type.mandatorySystemGestures()
                    | WindowInsets.Type.tappableElement() | WindowInsets.Type.displayCutout());
                circles.setPadding(safe.left, safe.top, safe.right, safe.bottom);
                circles.refreshCircles();
                actionsPosition.topMargin = safe.top + topMargin;
                actionsPosition.rightMargin = safe.right + rightMargin;
                actions.setLayoutParams(actionsPosition);
                return insets;
            });
        }
        setContentView(page);
    }

    @Override
    public void onWindowFocusChanged(boolean focused) {
        super.onWindowFocusChanged(focused);
        if (focused) {
            hideSystemBars();
        }
    }

    @Override
    protected void onSaveInstanceState(Bundle state) {
        super.onSaveInstanceState(state);
        if (draft != null) {
            draft.writeTo(state);
        }
    }

    @SuppressWarnings("deprecation") // Android 11–14 require an explicit edge-to-edge request.
    private void configureFullscreen() {
        Window window = getWindow();
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            if (Build.VERSION.SDK_INT < Build.VERSION_CODES.VANILLA_ICE_CREAM) {
                window.setDecorFitsSystemWindows(false);
            }
            WindowManager.LayoutParams attributes = window.getAttributes();
            attributes.layoutInDisplayCutoutMode =
                WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_ALWAYS;
            window.setAttributes(attributes);
        }
        hideSystemBars();
    }

    @SuppressWarnings("deprecation") // Android before API 30 uses the legacy immersive flags.
    private void hideSystemBars() {
        Window window = getWindow();
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            WindowInsetsController controller = window.getInsetsController();
            if (controller != null) {
                controller.setSystemBarsBehavior(
                    WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
                controller.hide(WindowInsets.Type.systemBars());
            }
        } else {
            window.getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_FULLSCREEN
                | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                | View.SYSTEM_UI_FLAG_LAYOUT_STABLE);
            window.addFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN);
            window.clearFlags(WindowManager.LayoutParams.FLAG_FORCE_NOT_FULLSCREEN);
        }
    }

    private int dp(int value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }

    private void addAction(LinearLayout row, String symbol, String description, int color,
            Runnable action) {
        Button button = new Button(this);
        button.setText(symbol);
        button.setTextColor(color);
        button.setTextSize(ACTION_TEXT_SP);
        button.setContentDescription(description);
        button.setPadding(0, 0, 0, 0);
        button.setOnClickListener(view -> action.run());
        row.addView(button, new LinearLayout.LayoutParams(dp(ACTION_SIZE_DP), dp(ACTION_SIZE_DP)));
    }

    // Order matches the SDL TOUCH_CONTROL enum.
    private enum Control {
        ESCAPE("ESC", 0, 0),
        LEFT(null, -1, 0),
        RIGHT(null, 1, 0),
        SHIFT_UP("SHIFT+", 0, -1),
        SHIFT_DOWN("SHIFT-", 0, 1),
        REWIND("Q", 0, 0),
        ACCELERATE(null, 0, -1),
        BRAKE(null, 0, 1),
        CAMERA("C", 0, 0),
        FOLLOW("T", 0, 0);

        final String label;
        final int arrowX;
        final int arrowY;

        Control(String label, int arrowX, int arrowY) {
            this.label = label;
            this.arrowX = arrowX;
            this.arrowY = arrowY;
        }
    }

    private final class CircleView extends View {
        private static final int NO_SELECTION = -1;
        private static final int NO_POINTER = -1;
        private static final int CIRCLE_COORDINATES = 3;
        private static final int FILL_ALPHA = 38;
        private static final int DRAG_ALPHA = 110;
        private static final int OUTLINE_ALPHA = 210;
        private static final float ARROW_SCALE = 0.36f;
        private static final float LABEL_OFFSET = 0.20f;
        private static final float LABEL_SIZE = 0.60f;
        private static final float LABEL_WIDTH = 1.50f;
        private final Control[] controls = Control.values();
        private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
        private float[] geometry;
        private int dragControl = NO_SELECTION;
        private int dragPointer = NO_POINTER;
        private float dragOffsetX;
        private float dragOffsetY;
        private float lastPointerX;
        private float lastPointerY;

        CircleView() {
            super(ControlLayoutEditorActivity.this);
            paint.setColor(Color.WHITE);
            paint.setStrokeWidth(getResources().getDisplayMetrics().density);
            paint.setTextAlign(Paint.Align.CENTER);
        }

        @Override
        protected void onSizeChanged(int width, int height, int oldWidth, int oldHeight) {
            refreshCircles();
        }

        private int usableWidth() {
            return getWidth() - getPaddingLeft() - getPaddingRight();
        }

        private int usableHeight() {
            return getHeight() - getPaddingTop() - getPaddingBottom();
        }

        private void refreshCircles() {
            int width = usableWidth();
            int height = usableHeight();
            geometry = width > 0 && height > 0 ? draft.circles(width, height) : null;
            invalidate();
        }

        @Override
        protected void onDraw(Canvas canvas) {
            if (geometry == null) {
                return;
            }
            int saved = canvas.save();
            canvas.translate(getPaddingLeft(), getPaddingTop());
            for (int index = 0; index < ControlLayout.CONTROL_COUNT; index++) {
                if (index != dragControl) {
                    drawCircle(canvas, index, FILL_ALPHA);
                }
            }
            if (dragControl != NO_SELECTION) {
                drawCircle(canvas, dragControl, DRAG_ALPHA);
            }
            canvas.restoreToCount(saved);
        }

        private void drawCircle(Canvas canvas, int index, int alpha) {
            int base = index * CIRCLE_COORDINATES;
            float x = geometry[base];
            float y = geometry[base + 1];
            float radius = geometry[base + 2];
            if (radius <= 0f) {
                return;
            }
            paint.setStyle(Paint.Style.FILL);
            paint.setAlpha(alpha);
            canvas.drawCircle(x, y, radius, paint);
            paint.setStyle(Paint.Style.STROKE);
            paint.setAlpha(OUTLINE_ALPHA);
            canvas.drawCircle(x, y, radius, paint);

            Control control = controls[index];
            boolean hasArrow = control.arrowX != 0 || control.arrowY != 0;
            if (hasArrow) {
                float arrowY = control.label == null ? y : y - radius * LABEL_OFFSET;
                float dx = control.arrowX * radius * ARROW_SCALE;
                float dy = control.arrowY * radius * ARROW_SCALE;
                canvas.drawLine(x - dx, arrowY - dy, x + dx, arrowY + dy, paint);
                canvas.drawLine(x + dx, arrowY + dy, x + dy / 2f, arrowY - dx / 2f, paint);
                canvas.drawLine(x + dx, arrowY + dy, x - dy / 2f, arrowY + dx / 2f, paint);
            }
            if (control.label != null) {
                paint.setStyle(Paint.Style.FILL);
                paint.setTextSize(radius * LABEL_SIZE);
                if (control.label.length() > 1) {
                    paint.setTextSize(paint.getTextSize() * radius * LABEL_WIDTH
                        / paint.measureText(control.label));
                }
                float labelY = hasArrow ? y + radius * LABEL_OFFSET : y;
                canvas.drawText(control.label, x, labelY - (paint.ascent() + paint.descent()) / 2f, paint);
            }
        }

        private int findCircle(float x, float y) {
            int closest = NO_SELECTION;
            float nearest = Float.POSITIVE_INFINITY;
            // At an identical center, select the circle drawn on top.
            for (int index = ControlLayout.CONTROL_COUNT - 1; index >= 0; index--) {
                int base = index * CIRCLE_COORDINATES;
                float dx = x - geometry[base];
                float dy = y - geometry[base + 1];
                float radius = geometry[base + 2];
                float distance = dx * dx + dy * dy;
                if (distance <= radius * radius && distance < nearest) {
                    closest = index;
                    nearest = distance;
                }
            }
            return closest;
        }

        private void moveCircle(MotionEvent event, int pointerIndex) {
            float pointerX = event.getX(pointerIndex);
            float pointerY = event.getY(pointerIndex);
            if (pointerX == lastPointerX && pointerY == lastPointerY) {
                return;
            }
            int width = usableWidth();
            int height = usableHeight();
            if (width <= 0 || height <= 0) {
                return;
            }
            lastPointerX = pointerX;
            lastPointerY = pointerY;
            float x = (pointerX - getPaddingLeft() + dragOffsetX) / width;
            float y = (pointerY - getPaddingTop() + dragOffsetY) / height;
            draft.move(dragControl, x, y);
            refreshCircles();
        }

        @Override
        public boolean onTouchEvent(MotionEvent event) {
            int action = event.getActionMasked();
            if (action == MotionEvent.ACTION_DOWN) {
                if (geometry == null) {
                    return false;
                }
                lastPointerX = event.getX();
                lastPointerY = event.getY();
                float x = lastPointerX - getPaddingLeft();
                float y = lastPointerY - getPaddingTop();
                dragControl = findCircle(x, y);
                if (dragControl == NO_SELECTION) {
                    return false;
                }
                dragPointer = event.getPointerId(0);
                int base = dragControl * CIRCLE_COORDINATES;
                dragOffsetX = geometry[base] - x;
                dragOffsetY = geometry[base + 1] - y;
                invalidate();
                return true;
            }
            if (dragControl == NO_SELECTION) {
                return false;
            }
            int pointerIndex = event.findPointerIndex(dragPointer);
            boolean released = action == MotionEvent.ACTION_UP
                || (action == MotionEvent.ACTION_POINTER_UP && event.getActionIndex() == pointerIndex);
            if (pointerIndex >= 0 && (action == MotionEvent.ACTION_MOVE || released)) {
                moveCircle(event, pointerIndex);
            }
            if (released || action == MotionEvent.ACTION_CANCEL) {
                dragControl = NO_SELECTION;
                dragPointer = NO_POINTER;
                invalidate();
            }
            return true;
        }
    }
}
