#include "registration_settings.hpp"
#include "parameter_form.hpp"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFontMetrics>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QString>
#include <QVariant>
#include <QVBoxLayout>

namespace mif::desktop {

RegistrationSettings::RegistrationSettings(QWidget* parent) : QGroupBox(parent) {
    const RegistrationOptions defaults;
    setObjectName("registrationSettings");
    // 页签已经标明“配准”，本控件不再重复绘制标题；外框可由主窗口样式统一处理。
    setTitle(QString());
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 14, 12, 14);
    layout->setSpacing(12);
    layout->setAlignment(Qt::AlignTop);

    auto* method_label = new QLabel(QStringLiteral("配准方法"), this);
    method_ = new ScrollSafeWidget<QComboBox>(this);
    method_->setObjectName("registrationMethod");
    // 算法与变换模型分别保存枚举值，界面条目顺序不参与计算配置的判断。
    method_->addItem(QStringLiteral("关闭（图片已对齐）"), static_cast<int>(RegistrationMethod::None));
    method_->addItem(QStringLiteral("ECC"), static_cast<int>(RegistrationMethod::Ecc));
    method_->addItem(QStringLiteral("SIFT"), static_cast<int>(RegistrationMethod::Sift));
    method_->setCurrentIndex(method_->findData(static_cast<int>(defaults.method)));
    method_->setToolTip(QStringLiteral(
        "以第一张图片为参考，配准后裁剪所有图片共有的区域。\n"
        "ECC 根据灰度相关性估计变换，适合初始对齐较好的图片，可另选变换模型。\n"
        "SIFT 根据共同纹理匹配特征点，固定使用单应性变换。"));
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

    // RANSAC 标签最长，各组统一预留此宽度；控件自身的 sizeHint 保证数值可读。
    const int label_width = fontMetrics().horizontalAdvance(QStringLiteral("RANSAC 阈值"));
    common_fields_ = new QWidget(this);
    common_fields_->setObjectName("registrationCommonFields");
    auto* common_form = createParameterForm(common_fields_);
    max_size_ = new ScrollSafeWidget<QSpinBox>(common_fields_);
    max_size_->setObjectName("registrationMaxSize");
    max_size_->setRange(16, 8192);
    max_size_->setSuffix(QStringLiteral(" px"));
    max_size_->setToolTip(QStringLiteral(
        "范围：16–8192 px。估计配准变换时，图片最长边的上限。\n"
        "较小的值可加快计算，小图不会被放大；工作图像短边须至少为 16 px。\n"
        "最终仍在原始分辨率上对齐图片。"));
    addParameter(common_form, QStringLiteral("工作最长边"), max_size_, label_width);
    layout->addWidget(common_fields_);

    ecc_fields_ = new QWidget(this);
    ecc_fields_->setObjectName("registrationEccFields");
    auto* ecc_form = createParameterForm(ecc_fields_);
    motion_model_ = new ScrollSafeWidget<QComboBox>(ecc_fields_);
    motion_model_->setObjectName("registrationMotionModel");
    motion_model_->addItem(QStringLiteral("平移"), static_cast<int>(MotionModel::Translation));
    motion_model_->addItem(QStringLiteral("仿射"), static_cast<int>(MotionModel::Affine));
    motion_model_->addItem(QStringLiteral("单应性"), static_cast<int>(MotionModel::Homography));
    motion_model_->setCurrentIndex(motion_model_->findData(static_cast<int>(defaults.motion_model)));
    motion_model_->setToolTip(QStringLiteral(
        "仅用于 ECC，决定允许估计的几何变换。\n"
        "平移：仅校正水平和垂直位移。\n"
        "仿射：可校正平移、旋转、缩放和剪切。\n"
        "单应性：可校正平面透视变化，需要较好的初始对齐。"));
    addParameter(ecc_form, QStringLiteral("变换模型"), motion_model_, label_width);

    iterations_ = new ScrollSafeWidget<QSpinBox>(ecc_fields_);
    iterations_->setObjectName("registrationIterations");
    iterations_->setRange(1, 10000);
    iterations_->setToolTip(QStringLiteral(
        "范围：1–10000。每张图片执行 ECC 配准的最大迭代次数。\n"
        "增大可为收敛预留更多计算时间，但不能保证配准成功。"));
    addParameter(ecc_form, QStringLiteral("迭代上限"), iterations_, label_width);

    epsilon_ = new ScrollSafeWidget<QDoubleSpinBox>(ecc_fields_);
    epsilon_->setObjectName("registrationEpsilon");
    epsilon_->setDecimals(8);
    epsilon_->setRange(1e-8, 1.0);
    epsilon_->setSingleStep(1e-5);
    epsilon_->setToolTip(QStringLiteral(
        "范围：0.00000001–1。ECC 相关系数变化小于此值时停止迭代。\n"
        "较小的值要求更严格的收敛，通常需要更多迭代。"));
    addParameter(ecc_form, QStringLiteral("收敛阈值"), epsilon_, label_width);
    layout->addWidget(ecc_fields_);

    sift_fields_ = new QWidget(this);
    sift_fields_->setObjectName("registrationSiftFields");
    auto* sift_form = createParameterForm(sift_fields_);
    max_features_ = new ScrollSafeWidget<QSpinBox>(sift_fields_);
    max_features_->setObjectName("registrationMaxFeatures");
    max_features_->setRange(64, 100000);
    max_features_->setToolTip(QStringLiteral(
        "范围：64–100000。每张工作图像最多保留的 SIFT 特征点数。\n"
        "增大可提供更多候选匹配，也会增加计算时间和内存占用。"));
    addParameter(sift_form, QStringLiteral("特征点上限"), max_features_, label_width);

    match_ratio_ = new ScrollSafeWidget<QDoubleSpinBox>(sift_fields_);
    match_ratio_->setObjectName("registrationMatchRatio");
    match_ratio_->setDecimals(2);
    match_ratio_->setRange(0.01, 0.99);
    match_ratio_->setSingleStep(0.01);
    match_ratio_->setToolTip(QStringLiteral(
        "范围：0.01–0.99。最近邻与次近邻特征描述子距离的比值阈值。\n"
        "比值低于此阈值才保留匹配；值越小，筛选越严格。"));
    addParameter(sift_form, QStringLiteral("匹配距离比"), match_ratio_, label_width);

    ransac_threshold_ = new ScrollSafeWidget<QDoubleSpinBox>(sift_fields_);
    ransac_threshold_->setObjectName("registrationRansacThreshold");
    ransac_threshold_->setDecimals(2);
    ransac_threshold_->setRange(0.01, 1000.0);
    ransac_threshold_->setSingleStep(0.1);
    ransac_threshold_->setSuffix(QStringLiteral(" px"));
    ransac_threshold_->setToolTip(QStringLiteral(
        "范围：0.01–1000 px。RANSAC 判定匹配点为内点的重投影误差上限。\n"
        "单位是工作分辨率下的像素；改变工作最长边后，相同数值对应的原图距离也会变化。"));
    addParameter(sift_form, QStringLiteral("RANSAC 阈值"), ransac_threshold_, label_width);

    min_inlier_ratio_ = new ScrollSafeWidget<QDoubleSpinBox>(sift_fields_);
    min_inlier_ratio_->setObjectName("registrationMinInlierRatio");
    min_inlier_ratio_->setDecimals(2);
    min_inlier_ratio_->setRange(0.01, 1.0);
    min_inlier_ratio_->setSingleStep(0.01);
    min_inlier_ratio_->setToolTip(QStringLiteral(
        "范围：0.01–1。RANSAC 内点占有效匹配的最低比例。\n"
        "低于此值会拒绝配准结果；同时还须至少有 6 个内点。"));
    addParameter(sift_form, QStringLiteral("最低内点比"), min_inlier_ratio_, label_width);
    layout->addWidget(sift_fields_);

    auto* reset = new QPushButton(QStringLiteral("恢复默认参数"), this);
    reset->setObjectName("resetRegistrationOptions");
    reset->setToolTip(QStringLiteral("恢复全部数值参数的默认值，保留当前配准算法与变换模型。"));
    layout->addWidget(reset);

    connect(method_, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int) { updateParameterVisibility(); });
    connect(motion_model_, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int) { updateParameterVisibility(); });
    connect(reset, &QPushButton::clicked, this, [this] { resetParameters(); });
    resetParameters();
    updateParameterVisibility();
}

RegistrationOptions RegistrationSettings::options() const {
    RegistrationOptions result;
    result.method = static_cast<RegistrationMethod>(method_->currentData().toInt());
    result.motion_model = static_cast<MotionModel>(motion_model_->currentData().toInt());
    result.max_size = max_size_->value();
    result.iterations = iterations_->value();
    result.epsilon = epsilon_->value();
    result.max_features = max_features_->value();
    result.match_ratio = match_ratio_->value();
    result.ransac_threshold = ransac_threshold_->value();
    result.min_inlier_ratio = min_inlier_ratio_->value();
    return result;
}

void RegistrationSettings::updateParameterVisibility() {
    const auto method = static_cast<RegistrationMethod>(method_->currentData().toInt());
    const auto model = static_cast<MotionModel>(motion_model_->currentData().toInt());
    const bool enabled = method != RegistrationMethod::None;
    const bool ecc = method == RegistrationMethod::Ecc;
    const bool sift = method == RegistrationMethod::Sift;
    if (!enabled)
        method_hint_->setText(QStringLiteral("已对齐可直接融合；选择配准方法后，可调整对应参数。"));
    else if (sift)
        method_hint_->setText(QStringLiteral("利用共同纹理匹配图片，固定使用单应性变换，适合位移和透视变化。"));
    else if (model == MotionModel::Homography)
        method_hint_->setText(QStringLiteral("适合初始对齐较好的透视变化，以第一张图片为参考。"));
    else if (model == MotionModel::Translation)
        method_hint_->setText(QStringLiteral("仅校正水平和垂直位移，以第一张图片为参考。"));
    else
        method_hint_->setText(QStringLiteral("校正小幅位移、旋转和倍率变化，以第一张图片为参考。"));
    common_fields_->setVisible(enabled);
    ecc_fields_->setVisible(ecc);
    sift_fields_->setVisible(sift);
}

void RegistrationSettings::resetParameters() {
    // 从公共配置类型读取数值默认值；算法与模型选择都保留，包括暂时隐藏的 ECC 模型。
    const RegistrationOptions defaults;
    max_size_->setValue(defaults.max_size);
    iterations_->setValue(defaults.iterations);
    epsilon_->setValue(defaults.epsilon);
    max_features_->setValue(defaults.max_features);
    match_ratio_->setValue(defaults.match_ratio);
    ransac_threshold_->setValue(defaults.ransac_threshold);
    min_inlier_ratio_->setValue(defaults.min_inlier_ratio);
}

} // 命名空间 mif::desktop
