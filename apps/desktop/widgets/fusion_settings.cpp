#include "fusion_settings.hpp"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFontMetrics>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QSizePolicy>
#include <QSpinBox>
#include <QString>
#include <QVariant>
#include <QVBoxLayout>
#include <QWheelEvent>

namespace mif::desktop {
namespace {

/// 参数页的滚轮交给父滚动区，点击箭头及键盘编辑仍使用 Qt 默认行为。
template <typename Widget>
class ScrollSafeWidget final : public Widget {
public:
    explicit ScrollSafeWidget(QWidget* parent = nullptr) : Widget(parent) {}
protected:
    void wheelEvent(QWheelEvent* event) override { event->ignore(); }
};

/// 统一标签列的宽度，避免切换方法时数值列跳动；标签也提供字段的中文说明。
void addParameter(QFormLayout* form, const QString& title, QWidget* field, int label_width, int row = -1) {
    auto* label = new QLabel(title);
    label->setMinimumWidth(label_width);
    label->setToolTip(field->toolTip());
    label->setBuddy(field);
    field->setAccessibleName(title);
    field->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    if (row < 0) form->addRow(label, field);
    else form->insertRow(row, label, field);
}

/// 正则项作用于 [0, 1] 灰度图的方差；下限避免官方浮点引导滤波在平坦区除零。
QDoubleSpinBox* createEpsilon(QWidget* parent, const QString& name, double default_value) {
    auto* field = new ScrollSafeWidget<QDoubleSpinBox>(parent);
    field->setObjectName(name);
    field->setDecimals(8);
    field->setRange(1e-6, 1.0);
    field->setSingleStep(default_value);
    field->setValue(default_value);
    field->setToolTip(QStringLiteral(
        "范围：0.000001–1。引导滤波的正则项，与归一化灰度图的局部方差相加。\n"
        "数值越大，通常越倾向于平滑融合权重；数值越小，越贴近引导图中的边缘。\n"
        "最小值受官方引导滤波的浮点精度限制，防止平坦区域发生除零。"));
    return field;
}

/// 半径按输入图像的原始像素计，实际窗口边长为 2 × 半径 + 1。
QSpinBox* createRadius(QWidget* parent, const QString& name, int default_value) {
    auto* field = new ScrollSafeWidget<QSpinBox>(parent);
    field->setObjectName(name);
    field->setRange(1, 255);
    field->setSuffix(QStringLiteral(" px"));
    field->setValue(default_value);
    return field;
}

/// 所有方法采用相同表单间距，独立容器保证整组显隐，不销毁用户填写的参数。
QFormLayout* createParameterForm(QWidget* panel) {
    auto* form = new QFormLayout(panel);
    form->setContentsMargins(0, 0, 0, 0);
    form->setHorizontalSpacing(8);
    form->setVerticalSpacing(8);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setRowWrapPolicy(QFormLayout::DontWrapRows);
    return form;
}

/// 各类局部统计窗口均要求奇数；提交偶数时向上调整，逐字输入期间保留用户文本。
QSpinBox* createOddWindow(QWidget* parent, const QString& name, int maximum, int default_value) {
    auto* field = new ScrollSafeWidget<QSpinBox>(parent);
    field->setObjectName(name);
    field->setRange(1, maximum);
    field->setSingleStep(2);
    field->setKeyboardTracking(false);
    field->setValue(default_value);
    QObject::connect(field, qOverload<int>(&QSpinBox::valueChanged), field,
                     [field](int value) { if (value % 2 == 0) field->setValue(value + 1); });
    return field;
}

/// 比例和归一化差异阈值均允许端点 0 与 1，六位小数便于微调较小阈值。
QDoubleSpinBox* createRatio(QWidget* parent, const QString& name, double default_value, double step) {
    auto* field = new ScrollSafeWidget<QDoubleSpinBox>(parent);
    field->setObjectName(name);
    field->setDecimals(6);
    field->setRange(0.0, 1.0);
    field->setSingleStep(step);
    field->setValue(default_value);
    return field;
}

} // 匿名命名空间

FusionSettings::FusionSettings(QWidget* parent) : QGroupBox(parent) {
    const FusionOptions defaults;
    setObjectName("fusionSettings");
    setTitle(QString());
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 14, 12, 14);
    layout->setSpacing(12);
    layout->setAlignment(Qt::AlignTop);

    auto* method_label = new QLabel(QStringLiteral("融合方法"), this);
    method_ = new ScrollSafeWidget<QComboBox>(this);
    method_->setObjectName("fusionMethod");
    method_->addItem(QStringLiteral("双尺度引导滤波"), static_cast<int>(FusionMethod::GuidedFilter));
    method_->addItem(QStringLiteral("拉普拉斯金字塔"), static_cast<int>(FusionMethod::LaplacianPyramid));
    method_->addItem(QStringLiteral("块方差（DCT）"), static_cast<int>(FusionMethod::Dct));
    method_->addItem(QStringLiteral("双树复小波（DTCWT）"), static_cast<int>(FusionMethod::Dtcwt));
    method_->addItem(QStringLiteral("GFG-FGF 梯度融合"), static_cast<int>(FusionMethod::Gfgfgf));
    method_->setCurrentIndex(method_->findData(static_cast<int>(defaults.method)));
    method_->setToolTip(QStringLiteral("将各张图片中的清晰区域合成一张图像；各方法的参数分别保存。"));
    method_label->setBuddy(method_);
    method_->setAccessibleName(method_label->text());
    auto* method_layout = new QVBoxLayout;
    method_layout->setSpacing(6);
    method_layout->addWidget(method_label);
    method_layout->addWidget(method_);
    layout->addLayout(method_layout);
    method_hint_ = new QLabel(this);
    method_hint_->setObjectName("parameterHint");
    method_hint_->setWordWrap(true);
    layout->addWidget(method_hint_);

    const int label_width = fontMetrics().horizontalAdvance(QStringLiteral("细节正则项"));
    // 重用构造步骤，但每次都创建独立控件；清晰度配置也不会在两种方法之间串用。
    auto createMethodForm = [&](MethodFields& fields, const QString& prefix, const QString& panel_name,
                                const FocusOptions& focus, int radius, double epsilon) {
        fields.panel = new QWidget(this);
        fields.panel->setObjectName(panel_name);
        auto* form = createParameterForm(fields.panel);
        fields.focus = new ScrollSafeWidget<QComboBox>(fields.panel);
        fields.focus->setObjectName(prefix + "FocusMeasure");
        fields.focus->addItem(QStringLiteral("改进拉普拉斯"), static_cast<int>(FocusMeasure::ModifiedLaplacian));
        fields.focus->addItem(QStringLiteral("Tenengrad 梯度"), static_cast<int>(FocusMeasure::Tenengrad));
        fields.focus->setCurrentIndex(fields.focus->findData(static_cast<int>(focus.measure)));
        fields.focus->setToolTip(QStringLiteral(
            "比较各张图片局部的清晰程度。改进拉普拉斯使用二阶差分，Tenengrad 使用梯度能量。\n"
            "该指标只属于当前融合方法，另一种方法可以使用不同设置。"));
        addParameter(form, QStringLiteral("清晰度指标"), fields.focus, label_width);
        fields.window = createOddWindow(fields.panel, prefix + "FocusWindow", 255, focus.window);
        fields.window->setSuffix(QStringLiteral(" px"));
        fields.window->setToolTip(QStringLiteral(
            "范围：1–255 px 的奇数。清晰度响应的局部平均窗口边长。\n"
            "增大可抑制噪声，但可能损失细小结构；输入偶数时会调整为下一个奇数。"));
        addParameter(form, QStringLiteral("统计窗口"), fields.window, label_width);
        fields.detail_radius = createRadius(fields.panel, prefix + "DetailRadius", radius);
        fields.detail_radius->setToolTip(QStringLiteral(
            "范围：1–255 px。细节权重图的引导滤波半径，实际窗口边长为 2 × 半径 + 1。\n"
            "增大可平滑权重过渡，但可能使细小边界变宽。"));
        addParameter(form, QStringLiteral("细节半径"), fields.detail_radius, label_width);
        fields.detail_epsilon = createEpsilon(fields.panel, prefix + "DetailEpsilon", epsilon);
        addParameter(form, QStringLiteral("细节正则项"), fields.detail_epsilon, label_width);
        layout->addWidget(fields.panel);
        return form;
    };

    const auto& guided_defaults = defaults.guided_filter;
    auto* guided_form = createMethodForm(guided_, QStringLiteral("guided"), QStringLiteral("guidedFilterFields"),
                                         guided_defaults.focus, guided_defaults.detail_radius, guided_defaults.detail_epsilon);
    base_radius_ = createRadius(guided_.panel, QStringLiteral("guidedBaseRadius"), guided_defaults.base_radius);
    base_radius_->setToolTip(QStringLiteral(
        "范围：1–255 px。基础层均值滤波和基础权重引导滤波共用的半径。\n"
        "实际窗口边长为 2 × 半径 + 1；仅双尺度引导滤波使用。"));
    // 基础层参数位于清晰度之后、细节层之前，表单顺序与分解和重建的阅读顺序一致。
    addParameter(guided_form, QStringLiteral("基础半径"), base_radius_, label_width, 2);
    base_epsilon_ = createEpsilon(guided_.panel, QStringLiteral("guidedBaseEpsilon"), guided_defaults.base_epsilon);
    addParameter(guided_form, QStringLiteral("基础正则项"), base_epsilon_, label_width, 3);

    const auto& pyramid_defaults = defaults.laplacian_pyramid;
    auto* pyramid_form = createMethodForm(pyramid_, QStringLiteral("pyramid"), QStringLiteral("laplacianPyramidFields"),
                                          pyramid_defaults.focus, pyramid_defaults.detail_radius, pyramid_defaults.detail_epsilon);
    levels_ = new ScrollSafeWidget<QSpinBox>(pyramid_.panel);
    levels_->setObjectName("pyramidLevels");
    levels_->setRange(1, 16);
    levels_->setValue(pyramid_defaults.levels);
    levels_->setToolTip(QStringLiteral(
        "范围：1–16，包含最粗层。实际层数还受图片尺寸限制。\n"
        "设为 1 时直接进行单尺度加权；更高层数同时处理更多尺度的细节。"));
    addParameter(pyramid_form, QStringLiteral("金字塔层数"), levels_, label_width);

    auto createPanel = [&](QWidget*& panel, const QString& name) {
        panel = new QWidget(this);
        panel->setObjectName(name);
        layout->addWidget(panel);
        return createParameterForm(panel);
    };
    auto* dct_form = createPanel(dct_.panel, QStringLiteral("dctFields"));
    dct_.block_size = new ScrollSafeWidget<QSpinBox>(dct_.panel);
    dct_.block_size->setObjectName("dctBlockSize");
    dct_.block_size->setRange(2, 128);
    dct_.block_size->setSuffix(QStringLiteral(" px"));
    dct_.block_size->setValue(defaults.dct.block_size);
    dct_.block_size->setToolTip(QStringLiteral(
        "范围：2–128 px。以不重叠方块的像素方差比较清晰度，每个方块选择一个来源。\n"
        "较小方块可区分细小区域，较大方块的统计更稳定。"));
    addParameter(dct_form, QStringLiteral("方块边长"), dct_.block_size, label_width);
    dct_.consistency_window = createOddWindow(dct_.panel, QStringLiteral("dctConsistencyWindow"),
                                             31, defaults.dct.consistency_window);
    dct_.consistency_window->setSuffix(QStringLiteral(" 块"));
    dct_.consistency_window->setToolTip(QStringLiteral(
        "范围：1–31 块的奇数。对块级来源决策连续进行两次中值滤波，减少孤立选择。\n"
        "1 表示不做平滑；偶数会调整为下一个奇数。"));
    addParameter(dct_form, QStringLiteral("一致性窗口"), dct_.consistency_window, label_width);

    auto* dtcwt_form = createPanel(dtcwt_.panel, QStringLiteral("dtcwtFields"));
    dtcwt_.levels = new ScrollSafeWidget<QSpinBox>(dtcwt_.panel);
    dtcwt_.levels->setObjectName("dtcwtLevels");
    dtcwt_.levels->setRange(1, 16);
    dtcwt_.levels->setValue(defaults.dtcwt.levels);
    dtcwt_.levels->setToolTip(QStringLiteral(
        "范围：1–16。双树复小波的分解层数，实际可用层数受图像尺寸限制。\n"
        "增加层数可同时比较更大尺度的结构。"));
    addParameter(dtcwt_form, QStringLiteral("分解层数"), dtcwt_.levels, label_width);
    dtcwt_.activity_window = createOddWindow(dtcwt_.panel, QStringLiteral("dtcwtActivityWindow"),
                                            31, defaults.dtcwt.activity_window);
    dtcwt_.activity_window->setToolTip(QStringLiteral(
        "范围：1–31 的奇数。小波系数活动度的局部统计窗口，作用于各层系数。\n"
        "较大的窗口可稳定系数选择；偶数会调整为下一个奇数。"));
    addParameter(dtcwt_form, QStringLiteral("活动度窗口"), dtcwt_.activity_window, label_width);

    auto* gfgfgf_form = createPanel(gfgfgf_.panel, QStringLiteral("gfgfgfFields"));
    gfgfgf_.difference_window = createOddWindow(gfgfgf_.panel, QStringLiteral("gfgfgfDifferenceWindow"),
                                               255, defaults.gfgfgf.difference_window);
    gfgfgf_.difference_window->setSuffix(QStringLiteral(" px"));
    gfgfgf_.difference_window->setToolTip(QStringLiteral(
        "范围：1–255 px 的奇数。计算图像与其局部均值之差时使用的平均窗口。\n"
        "偶数会调整为下一个奇数。"));
    addParameter(gfgfgf_form, QStringLiteral("差异窗口"), gfgfgf_.difference_window, label_width);
    gfgfgf_.selection_ratio = createRatio(gfgfgf_.panel, QStringLiteral("gfgfgfSelectionRatio"),
                                         defaults.gfgfgf.selection_ratio, 0.01);
    gfgfgf_.selection_ratio->setToolTip(QStringLiteral(
        "范围：0–1。相对最大梯度能量的筛帧阈值，用于保留具有足够清晰细节的输入。\n"
        "数值越大，筛选越严格。"));
    addParameter(gfgfgf_form, QStringLiteral("梯度筛选比"), gfgfgf_.selection_ratio, label_width);
    gfgfgf_.difference_threshold = createRatio(gfgfgf_.panel, QStringLiteral("gfgfgfDifferenceThreshold"),
                                              defaults.gfgfgf.difference_threshold, 0.001);
    gfgfgf_.difference_threshold->setToolTip(QStringLiteral(
        "范围：0–1。忽略不大于此值的局部绝对差异，再生成融合决策。\n"
        "数值越小，对较细微的差异越敏感。"));
    addParameter(gfgfgf_form, QStringLiteral("差异阈值"), gfgfgf_.difference_threshold, label_width);
    gfgfgf_.guided_radius = createRadius(gfgfgf_.panel, QStringLiteral("gfgfgfGuidedRadius"),
                                        defaults.gfgfgf.guided_radius);
    gfgfgf_.guided_radius->setToolTip(QStringLiteral(
        "范围：1–255 px。两阶段引导滤波的窗口半径，控制融合权重过渡的平滑范围。"));
    addParameter(gfgfgf_form, QStringLiteral("引导半径"), gfgfgf_.guided_radius, label_width);
    gfgfgf_.guided_epsilon = createEpsilon(gfgfgf_.panel, QStringLiteral("gfgfgfGuidedEpsilon"),
                                          defaults.gfgfgf.guided_epsilon);
    addParameter(gfgfgf_form, QStringLiteral("引导正则项"), gfgfgf_.guided_epsilon, label_width);

    auto* reset = new QPushButton(QStringLiteral("恢复当前方法默认值"), this);
    reset->setObjectName("resetFusionOptions");
    reset->setToolTip(QStringLiteral("只恢复当前方法的参数，保留方法选择及其他方法的配置。"));
    layout->addWidget(reset);
    connect(method_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) { updateMethodVisibility(); });
    connect(reset, &QPushButton::clicked, this, [this] { resetCurrentMethod(); });
    updateMethodVisibility();
}

FusionOptions FusionSettings::options() const {
    FusionOptions result;
    result.method = static_cast<FusionMethod>(method_->currentData().toInt());
    auto readFocus = [](const MethodFields& fields) {
        FocusOptions focus;
        focus.measure = static_cast<FocusMeasure>(fields.focus->currentData().toInt());
        focus.window = fields.window->value();
        return focus;
    };
    result.guided_filter.focus = readFocus(guided_);
    result.guided_filter.base_radius = base_radius_->value();
    result.guided_filter.base_epsilon = base_epsilon_->value();
    result.guided_filter.detail_radius = guided_.detail_radius->value();
    result.guided_filter.detail_epsilon = guided_.detail_epsilon->value();
    result.laplacian_pyramid.focus = readFocus(pyramid_);
    result.laplacian_pyramid.detail_radius = pyramid_.detail_radius->value();
    result.laplacian_pyramid.detail_epsilon = pyramid_.detail_epsilon->value();
    result.laplacian_pyramid.levels = levels_->value();
    result.dct.block_size = dct_.block_size->value();
    result.dct.consistency_window = dct_.consistency_window->value();
    result.dtcwt.levels = dtcwt_.levels->value();
    result.dtcwt.activity_window = dtcwt_.activity_window->value();
    result.gfgfgf.difference_window = gfgfgf_.difference_window->value();
    result.gfgfgf.selection_ratio = gfgfgf_.selection_ratio->value();
    result.gfgfgf.difference_threshold = gfgfgf_.difference_threshold->value();
    result.gfgfgf.guided_radius = gfgfgf_.guided_radius->value();
    result.gfgfgf.guided_epsilon = gfgfgf_.guided_epsilon->value();
    // 桌面只展示融合图片，不要求核心保留每张输入的权重图，避免额外内存占用。
    // DTCWT 按尺度和方向选择系数，没有单一空间权重图；界面同样只使用其融合结果。
    return result;
}

void FusionSettings::updateMethodVisibility() {
    const auto method = static_cast<FusionMethod>(method_->currentData().toInt());
    guided_.panel->setVisible(method == FusionMethod::GuidedFilter);
    pyramid_.panel->setVisible(method == FusionMethod::LaplacianPyramid);
    dct_.panel->setVisible(method == FusionMethod::Dct);
    dtcwt_.panel->setVisible(method == FusionMethod::Dtcwt);
    gfgfgf_.panel->setVisible(method == FusionMethod::Gfgfgf);
    switch (method) {
    case FusionMethod::GuidedFilter:
        method_hint_->setText(QStringLiteral("使用 OpenCV 官方引导滤波，分别优化基础层和细节层权重后重建。"));
        break;
    case FusionMethod::LaplacianPyramid:
        method_hint_->setText(QStringLiteral("对不同尺度的细节分别加权，再重建清晰图像。"));
        break;
    case FusionMethod::Dct:
        method_hint_->setText(QStringLiteral("按方块的像素方差选取清晰来源，再平滑块级决策。"));
        break;
    case FusionMethod::Dtcwt:
        method_hint_->setText(QStringLiteral("用双树复小波比较各尺度、各方向的细节，再重建图像。"));
        break;
    case FusionMethod::Gfgfgf:
        method_hint_->setText(QStringLiteral("先按梯度筛选输入，再结合局部差异与引导滤波生成结果。"));
        break;
    }
}

void FusionSettings::resetCurrentMethod() {
    const FusionOptions defaults;
    auto resetCommon = [](MethodFields& fields, const FocusOptions& focus, int radius, double epsilon) {
        fields.focus->setCurrentIndex(fields.focus->findData(static_cast<int>(focus.measure)));
        fields.window->setValue(focus.window);
        fields.detail_radius->setValue(radius);
        fields.detail_epsilon->setValue(epsilon);
    };
    switch (static_cast<FusionMethod>(method_->currentData().toInt())) {
    case FusionMethod::GuidedFilter: {
        const auto& options = defaults.guided_filter;
        resetCommon(guided_, options.focus, options.detail_radius, options.detail_epsilon);
        base_radius_->setValue(options.base_radius);
        base_epsilon_->setValue(options.base_epsilon);
        break;
    }
    case FusionMethod::LaplacianPyramid: {
        const auto& options = defaults.laplacian_pyramid;
        resetCommon(pyramid_, options.focus, options.detail_radius, options.detail_epsilon);
        levels_->setValue(options.levels);
        break;
    }
    case FusionMethod::Dct:
        dct_.block_size->setValue(defaults.dct.block_size);
        dct_.consistency_window->setValue(defaults.dct.consistency_window);
        break;
    case FusionMethod::Dtcwt:
        dtcwt_.levels->setValue(defaults.dtcwt.levels);
        dtcwt_.activity_window->setValue(defaults.dtcwt.activity_window);
        break;
    case FusionMethod::Gfgfgf:
        gfgfgf_.difference_window->setValue(defaults.gfgfgf.difference_window);
        gfgfgf_.selection_ratio->setValue(defaults.gfgfgf.selection_ratio);
        gfgfgf_.difference_threshold->setValue(defaults.gfgfgf.difference_threshold);
        gfgfgf_.guided_radius->setValue(defaults.gfgfgf.guided_radius);
        gfgfgf_.guided_epsilon->setValue(defaults.gfgfgf.guided_epsilon);
        break;
    }
}

} // 命名空间 mif::desktop
