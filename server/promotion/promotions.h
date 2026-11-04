// ============================================================
// promotions.h - 促销引擎（策略模式 + 装饰模式 + 工厂模式）
//   促销类型：1打折 2直降 3满减券 4阶梯折扣（第2件9折/第3件8折/免单）
// ============================================================
#pragma once
#include <memory>
#include <string>
#include <vector>
#include <utility>
#include <algorithm>
#include <cmath>
#include "../model/models.h"
#include "../repository/repositories.h"

// 金额单位统一为"分"（int），避免浮点误差；促销策略内部按"元"计算后 round 回分
namespace Money {
    inline int toCents(double yuan) { return static_cast<int>(std::round(yuan * 100.0)); }
}

// ---------- 策略接口 ----------
class IPromotion {
public:
    virtual ~IPromotion() = default;
    virtual int apply(int cents) = 0;                          // 应用促销后的金额（分）
    // 明细感知版本：可按"同一商品购买数量"计算（阶梯折扣等数量相关促销重写此方法）
    virtual int applyToItems(int cents, const std::vector<OrderItem>& items) {
        (void)items;
        return apply(cents);
    }
    virtual int getDiscountAmount(int cents) = 0;              // 优惠金额（分）
    virtual std::string getDescription() const = 0;
    virtual int getId() const = 0;
};

// ---------- 具体策略：打折 ----------
class DiscountPromotion : public IPromotion {
public:
    DiscountPromotion(int id, double rate) : id_(id), rate_(std::clamp(rate, 0.0, 1.0)) {}
    int apply(int cents) override { return static_cast<int>(std::round(cents * rate_)); }
    int getDiscountAmount(int cents) override { return cents - apply(cents); }
    std::string getDescription() const override {
        return "打" + std::to_string(static_cast<int>(rate_ * 10)) + "折";
    }
    int getId() const override { return id_; }
private:
    int id_;
    double rate_;
};

// ---------- 具体策略：直降 ----------
class PriceCutPromotion : public IPromotion {
public:
    PriceCutPromotion(int id, double cutYuan) : id_(id), cutCents_(Money::toCents(std::max(0.0, cutYuan))) {}
    int apply(int cents) override { return cents > cutCents_ ? cents - cutCents_ : 0; }
    int getDiscountAmount(int cents) override { return cents > cutCents_ ? cutCents_ : cents; }
    std::string getDescription() const override {
        return "直降" + std::to_string(cutCents_ / 100) + "元";
    }
    int getId() const override { return id_; }
private:
    int id_;
    int cutCents_;
};

// ---------- 具体策略：满减券 ----------
class CouponPromotion : public IPromotion {
public:
    CouponPromotion(int id, double thresholdYuan, double reduceYuan)
        : id_(id), thresholdCents_(Money::toCents(std::max(0.0, thresholdYuan))),
          reduceCents_(Money::toCents(std::max(0.0, reduceYuan))) {}
    int apply(int cents) override {
        return cents >= thresholdCents_ ? cents - reduceCents_ : cents;
    }
    int getDiscountAmount(int cents) override {
        return cents >= thresholdCents_ ? reduceCents_ : 0;
    }
    std::string getDescription() const override {
        return "满" + std::to_string(thresholdCents_ / 100) +
               "减" + std::to_string(reduceCents_ / 100);
    }
    int getId() const override { return id_; }
private:
    int id_;
    int thresholdCents_;
    int reduceCents_;
};

// ---------- 具体策略：阶梯折扣（第2件9折 / 第3件8折 / 免单） ----------
// 规则 = 有序分段表 {件数上限, 折扣率}，如 [(1,1.0),(2,0.9)] 表示：
//   第1件原价、第2件及以后 9 折；[(2,1.0),(3,0.0)] 表示前2件原价、第3件起免单。
// 该促销按订单中每个明细项（同一商品）的数量独立分档计价，故重写 applyToItems。
class TierDiscountPromotion : public IPromotion {
public:
    struct Tier { int qty; double rate; };   // 件数上限 + 该档折扣率
    TierDiscountPromotion(int id, std::vector<Tier> rules)
        : id_(id), rules_(std::move(rules)) {
        rules_.erase(std::remove_if(rules_.begin(), rules_.end(), [](const Tier& tier) {
            return tier.qty <= 0 || !std::isfinite(tier.rate) || tier.rate < 0.0 || tier.rate > 1.0;
        }), rules_.end());
        std::sort(rules_.begin(), rules_.end(),
                  [](const Tier& a, const Tier& b) { return a.qty < b.qty; });
        rules_.erase(std::unique(rules_.begin(), rules_.end(), [](const Tier& a, const Tier& b) {
            return a.qty == b.qty;
        }), rules_.end());
    }

    // 无明细时无法分档，安全返回原价（不产生优惠）
    int apply(int cents) override { return cents; }

    // 明细感知：对每个明细按数量分档计价并累加（单位：分）
    int applyToItems(int cents, const std::vector<OrderItem>& items) override {
        (void)cents;
        long long total = 0;
        for (const auto& item : items) {
            total += tieredPrice(item.price, item.quantity);
        }
        return static_cast<int>(total);
    }

    int getDiscountAmount(int cents) override {
        return cents - apply(cents);
    }

    std::string getDescription() const override {
        if (rules_.empty()) return "阶梯折扣";
        std::string desc;
        int prev = 0;
        for (size_t i = 0; i < rules_.size(); ++i) {
            int from = prev + 1;
            int to   = rules_[i].qty;
            if (!desc.empty()) desc += "，";
            if (rules_[i].rate <= 0.0) {
                desc += "第" + std::to_string(from) + "件起免单";
            } else if (rules_[i].rate >= 0.999) {
                desc += "第" + std::to_string(from) + "件原价";
            } else {
                // 折扣率转中文折数：0.9 -> 9折（rate*10）
                int zhe = static_cast<int>(rules_[i].rate * 10 + 0.5);
                if (from == to) {
                    desc += "第" + std::to_string(from) + "件" +
                            std::to_string(zhe) + "折";
                } else {
                    desc += "第" + std::to_string(from) + "-" +
                            std::to_string(to) + "件" +
                            std::to_string(zhe) + "折";
                }
            }
            prev = rules_[i].qty;
        }
        return desc;
    }

    int getId() const override { return id_; }

private:
    // 单明细阶梯计价：第 qty 件商品按分段折扣累加（unit 单位：分）
    int tieredPrice(int unitCents, int qty) const {
        if (rules_.empty() || qty <= 0) return unitCents * qty;
        long long total = 0;
        int prev = 0;
        const double lastRate = rules_.back().rate;
        for (const auto& r : rules_) {
            if (qty <= prev) break;
            int upper = std::min(qty, r.qty);
            int cnt   = upper - prev;
            total    += static_cast<long long>(std::round(unitCents * cnt * r.rate));
            prev      = upper;
            if (upper >= qty) return static_cast<int>(total);   // 已覆盖全部件数
        }
        // 超出最高档的部分：按最后一档折扣延续
        total += static_cast<long long>(std::round(unitCents * (qty - prev) * lastRate));
        return static_cast<int>(total);
    }

    int id_;
    std::vector<Tier> rules_;
};

// ---------- 装饰器抽象基类 ----------
// 包装一个促销策略，可在其基础上叠加职责
class PromotionDecorator : public IPromotion {
public:
    explicit PromotionDecorator(std::unique_ptr<IPromotion> wrapped)
        : wrapped_(std::move(wrapped)) {}
    int apply(int cents) override {
        return wrapped_ ? wrapped_->apply(cents) : cents;
    }
    int applyToItems(int cents, const std::vector<OrderItem>& items) override {
        return wrapped_ ? wrapped_->applyToItems(cents, items) : cents;
    }
    int getDiscountAmount(int cents) override {
        return wrapped_ ? wrapped_->getDiscountAmount(cents) : 0;
    }
    std::string getDescription() const override {
        return wrapped_ ? wrapped_->getDescription() : "";
    }
    int getId() const override { return wrapped_ ? wrapped_->getId() : -1; }
protected:
    std::unique_ptr<IPromotion> wrapped_;
};

// ---------- 具体装饰器：额外折扣（折上折，如会员95折） ----------
class ExtraDiscountDecorator : public PromotionDecorator {
public:
    ExtraDiscountDecorator(std::unique_ptr<IPromotion> p, double extraRate)
        : PromotionDecorator(std::move(p)), extraRate_(extraRate) {}
    int apply(int cents) override {
        int base = PromotionDecorator::apply(cents);
        return static_cast<int>(std::round(base * extraRate_));
    }
    int applyToItems(int cents, const std::vector<OrderItem>& items) override {
        int base = PromotionDecorator::applyToItems(cents, items);
        return static_cast<int>(std::round(base * extraRate_));
    }
    int getDiscountAmount(int cents) override {
        return cents - apply(cents);
    }
    std::string getDescription() const override {
        return PromotionDecorator::getDescription() + " + 额外" +
               std::to_string(static_cast<int>(extraRate_ * 10)) + "折";
    }
private:
    double extraRate_;
};

// ---------- 具体装饰器：叠加另一个促销（先执行内层，再执行外层） ----------
class CompositeDecorator : public PromotionDecorator {
public:
    CompositeDecorator(std::unique_ptr<IPromotion> wrapped,
                       std::unique_ptr<IPromotion> extra)
        : PromotionDecorator(std::move(wrapped)), extra_(std::move(extra)) {}
    int apply(int cents) override {
        int afterBase = PromotionDecorator::apply(cents);
        return extra_ ? extra_->apply(afterBase) : afterBase;
    }
    int applyToItems(int cents, const std::vector<OrderItem>& items) override {
        // 先执行内层（含明细感知），再把结果交给外层促销
        int afterBase = PromotionDecorator::applyToItems(cents, items);
        return extra_ ? extra_->applyToItems(afterBase, items) : afterBase;
    }
    int getDiscountAmount(int cents) override {
        return cents - apply(cents);
    }
    std::string getDescription() const override {
        return PromotionDecorator::getDescription() +
               (extra_ ? "，再" + extra_->getDescription() : "");
    }
private:
    std::unique_ptr<IPromotion> extra_;
};

// ---------- 工厂：根据促销记录创建策略对象 ----------
class PromotionFactory {
public:
    explicit PromotionFactory(std::shared_ptr<PromotionRepository> repo)
        : repo_(std::move(repo)) {}

    std::unique_ptr<IPromotion> create(int promoId);
    std::unique_ptr<IPromotion> createFromRecord(const Promotion& p);

private:
    std::unique_ptr<IPromotion> createTierDiscount(const Promotion& p);  // type=4 阶梯折扣
    std::shared_ptr<PromotionRepository> repo_;
};

// ---------- 促销链构建器（装饰模式层层包装） ----------
class PromoChainBuilder {
public:
    static std::unique_ptr<IPromotion> build(const std::vector<int>& promoIds,
                                             PromotionFactory& factory);
};
