#pragma once

#include <mif/registration/options_base.hpp>

#include <QGroupBox>
#include <memory>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QSpinBox;

namespace mif::desktop {

/// 界面内部的表单选择；配准核心根据具体配置类型选择实现。
enum class RegistrationMethod { None, Ecc, Sift };

/// 独立配准设置面板：算法与变换模型分别选择，切换算法时保留模型与全部参数。
/// 仅负责收集界面配置；主窗口在启动任务时读取快照，再交给后台处理流程。
class RegistrationSettings : public QGroupBox {
public:
    /// 创建全部字段；控件由 Qt 父子关系管理，适合放在可滚动的参数页中。
    explicit RegistrationSettings(QWidget* parent = nullptr);

    /// 创建当前算法的独立配置；隐藏算法的控件值保留，但不传入本次计算。
    std::unique_ptr<RegistrationOptionsBase> options() const;

private:
    /// 按所选方法更新提示与参数组；ECC 和 SIFT 共享工作分辨率设置。
    void updateParameterVisibility();
    /// 恢复核心接口定义的默认数值参数，保留当前算法与变换模型。
    void resetParameters();

    QComboBox* method_ = nullptr;
    QComboBox* motion_model_ = nullptr;
    QLabel* method_hint_ = nullptr;

    // 以整组显隐代替销毁重建，保留切换前的输入值，也使标签与字段同步显隐。
    QWidget* common_fields_ = nullptr;
    QWidget* ecc_fields_ = nullptr;
    QWidget* sift_fields_ = nullptr;

    // 两种算法共用最长边上限；ECC 使用迭代次数/收敛阈值，SIFT 使用其余四项。
    QSpinBox* max_working_dimension_ = nullptr;
    QSpinBox* max_iterations_ = nullptr;
    QDoubleSpinBox* convergence_tolerance_ = nullptr;
    QSpinBox* max_features_ = nullptr;
    QDoubleSpinBox* match_ratio_threshold_ = nullptr;
    QDoubleSpinBox* ransac_reprojection_threshold_ = nullptr;
    QDoubleSpinBox* min_inlier_ratio_ = nullptr;
};

} // 命名空间 mif::desktop
