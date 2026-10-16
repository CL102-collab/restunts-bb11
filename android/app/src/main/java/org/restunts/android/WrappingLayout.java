package org.restunts.android;

import android.content.Context;
import android.view.View;
import android.view.ViewGroup;

/** Places settings in reading order, starting a new row when they no longer fit. */
public final class WrappingLayout extends ViewGroup {
    private final int horizontalSpacing;
    private final int verticalSpacing;

    public WrappingLayout(Context context, int horizontalSpacing, int verticalSpacing) {
        super(context);
        this.horizontalSpacing = horizontalSpacing;
        this.verticalSpacing = verticalSpacing;
    }

    @Override
    protected LayoutParams generateDefaultLayoutParams() {
        return new LayoutParams(LayoutParams.WRAP_CONTENT, LayoutParams.WRAP_CONTENT);
    }

    @Override
    protected void onMeasure(int widthMeasureSpec, int heightMeasureSpec) {
        int availableWidth = MeasureSpec.getMode(widthMeasureSpec) == MeasureSpec.UNSPECIFIED
            ? Integer.MAX_VALUE : Math.max(0, MeasureSpec.getSize(widthMeasureSpec)
                - getPaddingLeft() - getPaddingRight());
        int rowWidth = 0;
        int rowHeight = 0;
        int rowsWidth = 0;
        int rowsHeight = 0;
        int childState = 0;
        boolean hasChild = false;
        for (int index = 0; index < getChildCount(); index++) {
            View child = getChildAt(index);
            if (child.getVisibility() == GONE) {
                continue;
            }
            measureChild(child, widthMeasureSpec, heightMeasureSpec);
            int spacing = hasChild ? horizontalSpacing : 0;
            if (hasChild && child.getMeasuredWidth() > availableWidth - rowWidth - spacing) {
                rowsWidth = Math.max(rowsWidth, rowWidth);
                rowsHeight += rowHeight + verticalSpacing;
                rowWidth = 0;
                rowHeight = 0;
                spacing = 0;
            }
            rowWidth += spacing + child.getMeasuredWidth();
            rowHeight = Math.max(rowHeight, child.getMeasuredHeight());
            childState = combineMeasuredStates(childState, child.getMeasuredState());
            hasChild = true;
        }
        int desiredWidth = Math.max(rowsWidth, rowWidth) + getPaddingLeft() + getPaddingRight();
        int desiredHeight = rowsHeight + rowHeight + getPaddingTop() + getPaddingBottom();
        setMeasuredDimension(resolveSizeAndState(Math.max(getSuggestedMinimumWidth(), desiredWidth),
                widthMeasureSpec, childState),
            resolveSizeAndState(Math.max(getSuggestedMinimumHeight(), desiredHeight),
                heightMeasureSpec, childState << MEASURED_HEIGHT_STATE_SHIFT));
    }

    @Override
    protected void onLayout(boolean changed, int left, int top, int right, int bottom) {
        int availableWidth = Math.max(0, right - left - getPaddingLeft() - getPaddingRight());
        int rowStart = 0;
        int rowWidth = 0;
        int rowHeight = 0;
        int rowTop = getPaddingTop();
        boolean hasChild = false;
        for (int index = 0; index < getChildCount(); index++) {
            View child = getChildAt(index);
            if (child.getVisibility() == GONE) {
                continue;
            }
            int spacing = hasChild ? horizontalSpacing : 0;
            if (hasChild && child.getMeasuredWidth() > availableWidth - rowWidth - spacing) {
                layoutRow(rowStart, index, rowTop, rowHeight);
                rowTop += rowHeight + verticalSpacing;
                rowStart = index;
                rowWidth = 0;
                rowHeight = 0;
                spacing = 0;
            }
            rowWidth += spacing + child.getMeasuredWidth();
            rowHeight = Math.max(rowHeight, child.getMeasuredHeight());
            hasChild = true;
        }
        layoutRow(rowStart, getChildCount(), rowTop, rowHeight);
    }

    private void layoutRow(int start, int end, int top, int height) {
        boolean rtl = getLayoutDirection() == LAYOUT_DIRECTION_RTL;
        int cursor = rtl ? getWidth() - getPaddingRight() : getPaddingLeft();
        for (int index = start; index < end; index++) {
            View child = getChildAt(index);
            if (child.getVisibility() == GONE) {
                continue;
            }
            int childLeft = rtl ? cursor - child.getMeasuredWidth() : cursor;
            int childTop = top + (height - child.getMeasuredHeight()) / 2;
            child.layout(childLeft, childTop, childLeft + child.getMeasuredWidth(),
                childTop + child.getMeasuredHeight());
            cursor += (rtl ? -1 : 1) * (child.getMeasuredWidth() + horizontalSpacing);
        }
    }
}
