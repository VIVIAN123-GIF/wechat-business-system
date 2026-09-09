#include "HandlerFactory.h"
#include "../common/ProtocolHeader.h"

std::unique_ptr<ICommandHandler> HandlerFactory::create(uint32_t code, HandlerContext& ctx) {
    using namespace ProtocolConstants;
    switch (code) {
        case C_REQ_PRODUCT_LIST:   return std::make_unique<ShowProductHandler>(ctx);
        case C_REQ_PRODUCT_DETAIL: return std::make_unique<ProductDetailHandler>(ctx);
        case C_REQ_LOGIN:          return std::make_unique<LoginHandler>(ctx);
        case C_REQ_REGISTER:       return std::make_unique<RegisterHandler>(ctx);
        case C_REQ_ADD_CART:       return std::make_unique<AddCartHandler>(ctx);
        case C_REQ_PROMO_LIST:     return std::make_unique<PromoListHandler>(ctx);
        case C_REQ_CHECKOUT:       return std::make_unique<CheckoutHandler>(ctx);
        case C_REQ_ORDER_QUERY:    return std::make_unique<OrderQueryHandler>(ctx);
        case C_REQ_RETURN:         return std::make_unique<ReturnHandler>(ctx);
        case C_REQ_HEARTBEAT:      return std::make_unique<HeartbeatHandler>();
        default:                   return nullptr;
    }
}
