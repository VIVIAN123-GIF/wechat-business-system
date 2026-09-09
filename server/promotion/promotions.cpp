#include "promotions.h"
#include "../Logger.h"

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
        default:
            LOG_WARN("未知促销类型: id=" + std::to_string(p.id) +
                     " type=" + std::to_string(p.type));
            return nullptr;
    }
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
