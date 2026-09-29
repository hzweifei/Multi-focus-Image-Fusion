#pragma once

#include <mif/registration_options.hpp>

#include <QGroupBox>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QSpinBox;

namespace mif::desktop {

/// 独立配准设置面板：按方法显示相关参数，切换方法时保留所有已填写的值。
/// 仅负责收集界面配置；主窗口在启动任务时读取快照，再交给后台处理流程。
class RegistrationSettings : public QGroupBox {
public:
    /// 创建全部字段；控件由 Qt 父子关系管理，适合放在可滚动的参数页中。
    explicit RegistrationSettings(QWidget* parent = nullptr);

    /// 返回当前方法和全部参数，包括当前方法未显示的字段。
    RegistrationOptions options() const;

private:
    /// 按所选方法更新提示与参数组；ECC 和 SIFT 共享工作分辨率设置。
    void updateParameterVisibility();
    /// 恢复核心接口定义的默认参数，保留当前所选配准方法。
    void resetParameters();

    QComboBox* alignment_ = nullptr;
    QLabel* method_hint_ = nullptr;

    // 以整组显隐代替销毁重建，保留切换前的输入值，也使标签与字段同步显隐。
    QWidget* common_fields_ = nullptr;
    QWidget* ecc_fields_ = nullptr;
    QWidget* sift_fields_ = nullptr;

    // 所有方法共用最长边上限；ECC 使用迭代次数/收敛阈值，SIFT 使用其余四项。
    QSpinBox* max_size_ = nullptr;
    QSpinBox* iterations_ = nullptr;
    QDoubleSpinBox* epsilon_ = nullptr;
    QSpinBox* max_features_ = nullptr;
    QDoubleSpinBox* match_ratio_ = nullptr;
    QDoubleSpinBox* ransac_threshold_ = nullptr;
    QDoubleSpinBox* min_inlier_ratio_ = nullptr;
};

} // 命名空间 mif::desktop
