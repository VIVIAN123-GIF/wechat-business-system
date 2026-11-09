#include "HandlerFactory.h"
#include "../common/ProtocolHeader.h"

std::unique_ptr<ICommandHandler> HandlerFactory::create(uint32_t code, HandlerContext& ctx) {
    using namespace ProtocolConstants;
    switch (code) {
        case C_REQ_PRODUCT_LIST:   return std::make_unique<ShowProductHandler>(ctx);
        case C_REQ_PRODUCT_DETAIL: return std::make_unique<ProductDetailHandler>(ctx);
        case C_REQ_SEARCH:         return std::make_unique<SearchProductHandler>(ctx);
        case C_REQ_LOGIN:          return std::make_unique<LoginHandler>(ctx);
        case C_REQ_REGISTER:       return std::make_unique<RegisterHandler>(ctx);
        case C_REQ_ADD_CART:       return std::make_unique<AddCartHandler>(ctx);
        case C_REQ_PROMO_LIST:     return std::make_unique<PromoListHandler>(ctx);
        case C_REQ_PROMO_EXPLAIN:  return std::make_unique<PromoExplainHandler>(ctx);
        case C_REQ_GROUP_CREATE:   return std::make_unique<GroupCreateHandler>(ctx);
        case C_REQ_GROUP_JOIN:     return std::make_unique<GroupJoinHandler>(ctx);
        case C_REQ_GROUP_QUERY:    return std::make_unique<GroupQueryHandler>(ctx);
        case C_REQ_ORDER_STATUS:   return std::make_unique<OrderStatusHandler>(ctx);
        case C_REQ_ORDER_AUDIT:    return std::make_unique<OrderAuditHandler>(ctx);
        case C_REQ_PAY:            return std::make_unique<PaymentHandler>(ctx);
        case C_REQ_CANCEL_ORDER:   return std::make_unique<CancelOrderHandler>(ctx);
        case C_REQ_CONFIRM_RECEIPT:return std::make_unique<ConfirmReceiptHandler>(ctx);
        case C_REQ_ADDRESS_LIST:   return std::make_unique<AddressListHandler>(ctx);
        case C_REQ_ADDRESS_CREATE: return std::make_unique<AddressCreateHandler>(ctx);
        case C_REQ_REVIEW_CREATE:  return std::make_unique<ReviewCreateHandler>(ctx);
        case C_REQ_REVIEW_LIST:    return std::make_unique<ReviewListHandler>(ctx);
        case C_REQ_COUPON_LIST:    return std::make_unique<CouponListHandler>(ctx);
        case C_REQ_COUPON_CLAIM:   return std::make_unique<CouponClaimHandler>(ctx);
        case C_REQ_NOTIFICATION_LIST: return std::make_unique<NotificationListHandler>(ctx);
        case C_REQ_NOTIFICATION_READ: return std::make_unique<NotificationReadHandler>(ctx);
        case C_REQ_FAVORITE_LIST: return std::make_unique<FavoriteListHandler>(ctx);
        case C_REQ_FAVORITE_TOGGLE: return std::make_unique<FavoriteToggleHandler>(ctx);
        case C_REQ_CHECKIN: return std::make_unique<CheckinHandler>(ctx);
        case C_REQ_SHIPMENT_QUERY: return std::make_unique<ShipmentQueryHandler>(ctx);
        case C_REQ_SHIPMENT_CREATE: return std::make_unique<ShipmentCreateHandler>(ctx);
        case C_REQ_ADMIN_PRODUCT: return std::make_unique<AdminProductHandler>(ctx);
        case C_REQ_SKU_LIST: return std::make_unique<SkuListHandler>(ctx);
        case C_REQ_CHECKOUT:       return std::make_unique<CheckoutHandler>(ctx);
        case C_REQ_ORDER_QUERY:    return std::make_unique<OrderQueryHandler>(ctx);
        case C_REQ_RETURN:         return std::make_unique<ReturnHandler>(ctx);
        case C_REQ_STATS:          return std::make_unique<SalesStatsHandler>(ctx);
        case C_REQ_SERVICE_MSG:    return std::make_unique<ServiceMsgHandler>(ctx);
        case C_REQ_SERVICE_REPLY:  return std::make_unique<ServiceReplyHandler>(ctx);
        case C_REQ_RETURN_LIST:    return std::make_unique<ReturnListHandler>(ctx);
        case C_REQ_RETURN_AUDIT:   return std::make_unique<ReturnAuditHandler>(ctx);
        case C_REQ_RETURN_COMPLETE:return std::make_unique<ReturnCompleteHandler>(ctx);
        case C_REQ_HEARTBEAT:      return std::make_unique<HeartbeatHandler>();
        default:                   return nullptr;
    }
}
