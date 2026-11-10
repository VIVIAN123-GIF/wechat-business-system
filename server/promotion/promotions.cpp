// ============================================================
// promotions.cpp - 促销策略工厂 + 促销链组装（策略/装饰/工厂模式）
// ============================================================
#include "promotions.h"
#include "../Logger.h"

// 按促销 id 查库并创建对应策略对象
std::unique_ptr<IPromotion> PromotionFactory::create(int promoId) {
    auto record = repo_->findById(promoId);
    if (!record) {
        LOG_WARN("促销不存在: id=" + std::to_string(promoId));
        return nullptr;
    }
    if (record->status != 1) return nullptr;
    return createFromRecord(*record);
}

std::unique_ptr<IPromotion> PromotionFactory::createFromRecord(const Promotion& p) {
    switch (p.type) {
        case 1:  // 打折
            return std::make_unique<DiscountPromotion>(p.id, p.param1);
        case 2:  // 直降
            return std::make_unique<PriceCutPromotion>(p.id, p.param1);
        case 3:  // 满减券
            return std::make_unique<CouponPromotion>(p.id, p.param1, p.param2);
        case 4:  // 阶梯折扣（第2件9折/第3件8折/免单）
            return createTierDiscount(p);
        default:
            LOG_WARN("未知促销类型: id=" + std::to_string(p.id) +
                     " type=" + std::to_string(p.type));
            return nullptr;
    }
}

// 从促销记录解析阶梯分段：param1/param2=第1阶梯，param3/param4=第2阶梯，param5/param6=第3阶梯
std::unique_ptr<IPromotion> PromotionFactory::createTierDiscount(const Promotion& p) {
    std::vector<TierDiscountPromotion::Tier> rules;
    if (p.param1 > 0 && p.param2 >= 0.0) rules.push_back({static_cast<int>(p.param1), p.param2});
    if (p.param3 > 0 && p.param4 >= 0.0) rules.push_back({static_cast<int>(p.param3), p.param4});
    if (p.param5 > 0 && p.param6 >= 0.0) rules.push_back({static_cast<int>(p.param5), p.param6});
    if (rules.empty()) {
        LOG_WARN("阶梯折扣参数无效: id=" + std::to_string(p.id));
        return nullptr;
    }
    return std::make_unique<TierDiscountPromotion>(p.id, std::move(rules));
}

std::unique_ptr<IPromotion> PromoChainBuilder::build(const std::vector<int>& promoIds,
                                                     PromotionFactory& factory) {
    std::unique_ptr<IPromotion> chain = nullptr;
    for (int id : promoIds) {
        auto promo = factory.create(id);
        if (!promo) continue;
        if (!chain) {
            chain = std::move(promo);
        } else {
            // 装饰模式：将新促销包装到已有链上（先执行链内，再执行新促销）
            chain = std::make_unique<CompositeDecorator>(std::move(chain),
                                                         std::move(promo));
        }
    }
    return chain;
}
