// ============================================================
// promotions.h - 促销引擎（策略模式 + 装饰模式 + 工厂模式）
// ============================================================
#pragma once
#include <memory>
#include <string>
#include <vector>
#include "../model/models.h"
#include "../repository/repositories.h"

// ---------- 策略接口 ----------
class IPromotion {
public:
    virtual ~IPromotion() = default;
    virtual double apply(double price) = 0;                 // 应用促销后的金额
    virtual double getDiscountAmount(double price) = 0;     // 优惠金额
    virtual std::string getDescription() const = 0;
    virtual int getId() const = 0;
};

// ---------- 具体策略：打折 ----------
class DiscountPromotion : public IPromotion {
public:
    DiscountPromotion(int id, double rate) : id_(id), rate_(rate) {}
    double apply(double price) override { return price * rate_; }
    double getDiscountAmount(double price) override { return price * (1 - rate_); }
    std::string getDescription() const override {
        return "打" + std::to_string(static_cast<int>(rate_ * 100)) + "折";
    }
    int getId() const override { return id_; }
private:
    int id_;
    double rate_;
};

// ---------- 具体策略：直降 ----------
class PriceCutPromotion : public IPromotion {
public:
    PriceCutPromotion(int id, double cut) : id_(id), cut_(cut) {}
    double apply(double price) override { return price > cut_ ? price - cut_ : 0.0; }
    double getDiscountAmount(double price) override { return price > cut_ ? cut_ : price; }
    std::string getDescription() const override {
        return "直降" + std::to_string(static_cast<int>(cut_)) + "元";
    }
    int getId() const override { return id_; }
private:
    int id_;
    double cut_;
};

// ---------- 具体策略：满减券 ----------
class CouponPromotion : public IPromotion {
public:
    CouponPromotion(int id, double threshold, double reduce)
        : id_(id), threshold_(threshold), reduce_(reduce) {}
    double apply(double price) override {
        return price >= threshold_ ? price - reduce_ : price;
    }
    double getDiscountAmount(double price) override {
        return price >= threshold_ ? reduce_ : 0.0;
    }
    std::string getDescription() const override {
        return "满" + std::to_string(static_cast<int>(threshold_)) +
               "减" + std::to_string(static_cast<int>(reduce_));
    }
    int getId() const override { return id_; }
private:
    int id_;
    double threshold_;
    double reduce_;
};

// ---------- 装饰器抽象基类 ----------
// 包装一个促销策略，可在其基础上叠加职责
class PromotionDecorator : public IPromotion {
public:
    explicit PromotionDecorator(std::unique_ptr<IPromotion> wrapped)
        : wrapped_(std::move(wrapped)) {}
    double apply(double price) override {
        return wrapped_ ? wrapped_->apply(price) : price;
    }
    double getDiscountAmount(double price) override {
        return wrapped_ ? wrapped_->getDiscountAmount(price) : 0.0;
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
    double apply(double price) override {
        double base = PromotionDecorator::apply(price);
        return base * extraRate_;
    }
    double getDiscountAmount(double price) override {
        return price - apply(price);
    }
    std::string getDescription() const override {
        return PromotionDecorator::getDescription() + " + 额外" +
               std::to_string(static_cast<int>(extraRate_ * 100)) + "折";
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
    double apply(double price) override {
        double afterBase = PromotionDecorator::apply(price);
        return extra_ ? extra_->apply(afterBase) : afterBase;
    }
    double getDiscountAmount(double price) override {
        return price - apply(price);
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
    std::shared_ptr<PromotionRepository> repo_;
};

// ---------- 促销链构建器（装饰模式层层包装） ----------
class PromoChainBuilder {
public:
    static std::unique_ptr<IPromotion> build(const std::vector<int>& promoIds,
                                             PromotionFactory& factory);
};
