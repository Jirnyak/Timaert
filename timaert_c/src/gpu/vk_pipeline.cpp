#include "gpu/vk_pipeline.h"
#include "gpu/vk_common.h"
#include "gpu/vk_device.h"

#include <cstdio>
#include <vector>

namespace gpu
{
    namespace
    {
        bool read_file(const char* path, std::vector<char>& out)
        {
            std::FILE* f = std::fopen(path, "rb");
            if (!f) {
                std::fprintf(stderr, "[vk] cannot open %s\n", path);
                return false;
            }
            std::fseek(f, 0, SEEK_END);
            long n = std::ftell(f);
            std::fseek(f, 0, SEEK_SET);
            if (n <= 0) {
                std::fclose(f);
                std::fprintf(stderr, "[vk] empty shader %s\n", path);
                return false;
            }
            out.resize(static_cast<std::size_t>(n));
            std::size_t rd = std::fread(out.data(), 1,
                                        static_cast<std::size_t>(n), f);
            std::fclose(f);
            return rd == static_cast<std::size_t>(n);
        }

        bool make_module(VkDevice dev, const std::vector<char>& spv,
                         VkShaderModule* mod)
        {
            VkShaderModuleCreateInfo ci{};
            ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            ci.codeSize = spv.size();
            ci.pCode = reinterpret_cast<const std::uint32_t*>(spv.data());
            return vkCreateShaderModule(dev, &ci, nullptr, mod) == VK_SUCCESS;
        }
    } // namespace

    bool VulkanPipeline::create(const VulkanDevice& dev, VkRenderPass renderPass,
                                const char* vertSpvPath, const char* fragSpvPath,
                                std::uint32_t pushConstantBytes,
                                VkDescriptorSetLayout descriptorSetLayout)
    {
        std::vector<char> vsrc, fsrc;
        if (!read_file(vertSpvPath, vsrc)) return false;
        if (!read_file(fragSpvPath, fsrc)) return false;

        VkShaderModule vs = VK_NULL_HANDLE, fs = VK_NULL_HANDLE;
        if (!make_module(dev.device, vsrc, &vs)) return false;
        if (!make_module(dev.device, fsrc, &fs)) {
            vkDestroyShaderModule(dev.device, vs, nullptr);
            return false;
        }

        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vs;
        stages[0].pName = "main";
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = fs;
        stages[1].pName = "main";

        VkPipelineVertexInputStateCreateInfo vi{};
        vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

        VkPipelineInputAssemblyStateCreateInfo ia{};
        ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo vp{};
        vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vp.viewportCount = 1;
        vp.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo rs{};
        rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rs.polygonMode = VK_POLYGON_MODE_FILL;
        rs.cullMode = VK_CULL_MODE_NONE;
        rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rs.lineWidth = 1.0f;

        VkPipelineMultisampleStateCreateInfo ms{};
        ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineColorBlendAttachmentState cba{};
        cba.colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
            | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        cba.blendEnable = VK_FALSE;

        VkPipelineColorBlendStateCreateInfo cb{};
        cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cb.attachmentCount = 1;
        cb.pAttachments = &cba;

        // Depth disabled: the render pass carries a depth attachment (for 3D
        // meshes), but fullscreen passes ignore it.
        VkPipelineDepthStencilStateCreateInfo ds{};
        ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        ds.depthTestEnable = VK_FALSE;
        ds.depthWriteEnable = VK_FALSE;
        ds.depthCompareOp = VK_COMPARE_OP_ALWAYS;

        VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT,
                                VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dsi{};
        dsi.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dsi.dynamicStateCount = 2;
        dsi.pDynamicStates = dyn;

        VkPushConstantRange pcr{};
        pcr.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        pcr.offset = 0;
        pcr.size = pushConstantBytes;

        VkPipelineLayoutCreateInfo lci{};
        lci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        if (pushConstantBytes > 0) {
            lci.pushConstantRangeCount = 1;
            lci.pPushConstantRanges = &pcr;
        }
        if (descriptorSetLayout != VK_NULL_HANDLE) {
            lci.setLayoutCount = 1;
            lci.pSetLayouts = &descriptorSetLayout;
        }

        bool ok = vkCreatePipelineLayout(dev.device, &lci, nullptr, &layout)
                  == VK_SUCCESS;
        if (ok) {
            VkGraphicsPipelineCreateInfo gp{};
            gp.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
            gp.stageCount = 2;
            gp.pStages = stages;
            gp.pVertexInputState = &vi;
            gp.pInputAssemblyState = &ia;
            gp.pViewportState = &vp;
            gp.pRasterizationState = &rs;
            gp.pMultisampleState = &ms;
            gp.pColorBlendState = &cb;
            gp.pDepthStencilState = &ds;
            gp.pDynamicState = &dsi;
            gp.layout = layout;
            gp.renderPass = renderPass;
            gp.subpass = 0;
            ok = vkCreateGraphicsPipelines(dev.device, VK_NULL_HANDLE, 1, &gp,
                                           nullptr, &pipeline)
                 == VK_SUCCESS;
        }

        vkDestroyShaderModule(dev.device, vs, nullptr);
        vkDestroyShaderModule(dev.device, fs, nullptr);
        if (!ok) std::fprintf(stderr, "[vk] pipeline creation failed\n");
        return ok;
    }

    bool VulkanPipeline::create_mesh(
        const VulkanDevice& dev, VkRenderPass renderPass,
        const char* vertSpvPath, const char* fragSpvPath,
        std::uint32_t pushConstantBytes, std::uint32_t vertexStride,
        const VkVertexInputAttributeDescription* attrs, std::uint32_t attrCount,
        bool instanced, bool depthTest, bool depthWrite, bool blend,
        bool cullBack, VkDescriptorSetLayout descriptorSetLayout)
    {
        std::vector<char> vsrc, fsrc;
        if (!read_file(vertSpvPath, vsrc)) return false;
        if (!read_file(fragSpvPath, fsrc)) return false;

        VkShaderModule vs = VK_NULL_HANDLE, fs = VK_NULL_HANDLE;
        if (!make_module(dev.device, vsrc, &vs)) return false;
        if (!make_module(dev.device, fsrc, &fs)) {
            vkDestroyShaderModule(dev.device, vs, nullptr);
            return false;
        }

        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vs;
        stages[0].pName = "main";
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = fs;
        stages[1].pName = "main";

        VkVertexInputBindingDescription binding{};
        binding.binding = 0;
        binding.stride = vertexStride;
        binding.inputRate = instanced ? VK_VERTEX_INPUT_RATE_INSTANCE
                                      : VK_VERTEX_INPUT_RATE_VERTEX;
        VkPipelineVertexInputStateCreateInfo vi{};
        vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        // vertexStride == 0 => no vertex buffer (geometry from gl_VertexIndex).
        if (vertexStride > 0) {
            vi.vertexBindingDescriptionCount = 1;
            vi.pVertexBindingDescriptions = &binding;
            vi.vertexAttributeDescriptionCount = attrCount;
            vi.pVertexAttributeDescriptions = attrs;
        }

        VkPipelineInputAssemblyStateCreateInfo ia{};
        ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo vp{};
        vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vp.viewportCount = 1;
        vp.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo rs{};
        rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rs.polygonMode = VK_POLYGON_MODE_FILL;
        rs.cullMode = cullBack ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_NONE;
        rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rs.lineWidth = 1.0f;

        VkPipelineMultisampleStateCreateInfo ms{};
        ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineColorBlendAttachmentState cba{};
        cba.colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
            | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        cba.blendEnable = blend ? VK_TRUE : VK_FALSE;
        cba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        cba.colorBlendOp = VK_BLEND_OP_ADD;
        cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        cba.alphaBlendOp = VK_BLEND_OP_ADD;

        VkPipelineColorBlendStateCreateInfo cb{};
        cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cb.attachmentCount = 1;
        cb.pAttachments = &cba;

        VkPipelineDepthStencilStateCreateInfo ds{};
        ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        ds.depthTestEnable = depthTest ? VK_TRUE : VK_FALSE;
        ds.depthWriteEnable = depthWrite ? VK_TRUE : VK_FALSE;
        ds.depthCompareOp = VK_COMPARE_OP_LESS;

        VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT,
                                VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dsi{};
        dsi.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dsi.dynamicStateCount = 2;
        dsi.pDynamicStates = dyn;

        VkPushConstantRange pcr{};
        pcr.stageFlags =
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        pcr.offset = 0;
        pcr.size = pushConstantBytes;

        VkPipelineLayoutCreateInfo lci{};
        lci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        if (pushConstantBytes > 0) {
            lci.pushConstantRangeCount = 1;
            lci.pPushConstantRanges = &pcr;
        }
        if (descriptorSetLayout != VK_NULL_HANDLE) {
            lci.setLayoutCount = 1;
            lci.pSetLayouts = &descriptorSetLayout;
        }

        bool ok = vkCreatePipelineLayout(dev.device, &lci, nullptr, &layout)
                  == VK_SUCCESS;
        if (ok) {
            VkGraphicsPipelineCreateInfo gp{};
            gp.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
            gp.stageCount = 2;
            gp.pStages = stages;
            gp.pVertexInputState = &vi;
            gp.pInputAssemblyState = &ia;
            gp.pViewportState = &vp;
            gp.pRasterizationState = &rs;
            gp.pMultisampleState = &ms;
            gp.pColorBlendState = &cb;
            gp.pDepthStencilState = &ds;
            gp.pDynamicState = &dsi;
            gp.layout = layout;
            gp.renderPass = renderPass;
            gp.subpass = 0;
            ok = vkCreateGraphicsPipelines(dev.device, VK_NULL_HANDLE, 1, &gp,
                                           nullptr, &pipeline)
                 == VK_SUCCESS;
        }

        vkDestroyShaderModule(dev.device, vs, nullptr);
        vkDestroyShaderModule(dev.device, fs, nullptr);
        if (!ok) std::fprintf(stderr, "[vk] mesh pipeline creation failed\n");
        return ok;
    }

    bool VulkanPipeline::create_shadow(
        const VulkanDevice& dev, VkRenderPass shadowPass,
        const char* vertSpvPath, const char* fragSpvPath,
        std::uint32_t pushConstantBytes, std::uint32_t vertexStride,
        const VkVertexInputAttributeDescription* attrs, std::uint32_t attrCount,
        bool instanced)
    {
        std::vector<char> vsrc, fsrc;
        if (!read_file(vertSpvPath, vsrc)) return false;
        if (!read_file(fragSpvPath, fsrc)) return false;

        VkShaderModule vs = VK_NULL_HANDLE, fs = VK_NULL_HANDLE;
        if (!make_module(dev.device, vsrc, &vs)) return false;
        if (!make_module(dev.device, fsrc, &fs)) {
            vkDestroyShaderModule(dev.device, vs, nullptr);
            return false;
        }

        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vs;
        stages[0].pName = "main";
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = fs;
        stages[1].pName = "main";

        VkVertexInputBindingDescription binding{};
        binding.binding = 0;
        binding.stride = vertexStride;
        binding.inputRate = instanced ? VK_VERTEX_INPUT_RATE_INSTANCE
                                      : VK_VERTEX_INPUT_RATE_VERTEX;
        VkPipelineVertexInputStateCreateInfo vi{};
        vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vi.vertexBindingDescriptionCount = 1;
        vi.pVertexBindingDescriptions = &binding;
        vi.vertexAttributeDescriptionCount = attrCount;
        vi.pVertexAttributeDescriptions = attrs;

        VkPipelineInputAssemblyStateCreateInfo ia{};
        ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo vp{};
        vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vp.viewportCount = 1;
        vp.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo rs{};
        rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rs.polygonMode = VK_POLYGON_MODE_FILL;
        rs.cullMode = VK_CULL_MODE_NONE;
        rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rs.depthBiasEnable = VK_TRUE;
        rs.depthBiasConstantFactor = 1.25f;
        rs.depthBiasSlopeFactor = 1.75f;
        rs.lineWidth = 1.0f;

        VkPipelineMultisampleStateCreateInfo ms{};
        ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        // No colour attachment in the shadow (depth-only) pass.
        VkPipelineColorBlendStateCreateInfo cb{};
        cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cb.attachmentCount = 0;

        VkPipelineDepthStencilStateCreateInfo ds{};
        ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        ds.depthTestEnable = VK_TRUE;
        ds.depthWriteEnable = VK_TRUE;
        ds.depthCompareOp = VK_COMPARE_OP_LESS;

        VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT,
                                VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dsi{};
        dsi.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dsi.dynamicStateCount = 2;
        dsi.pDynamicStates = dyn;

        VkPushConstantRange pcr{};
        pcr.stageFlags =
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        pcr.offset = 0;
        pcr.size = pushConstantBytes;

        VkPipelineLayoutCreateInfo lci{};
        lci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        if (pushConstantBytes > 0) {
            lci.pushConstantRangeCount = 1;
            lci.pPushConstantRanges = &pcr;
        }

        bool ok = vkCreatePipelineLayout(dev.device, &lci, nullptr, &layout)
                  == VK_SUCCESS;
        if (ok) {
            VkGraphicsPipelineCreateInfo gp{};
            gp.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
            gp.stageCount = 2;
            gp.pStages = stages;
            gp.pVertexInputState = &vi;
            gp.pInputAssemblyState = &ia;
            gp.pViewportState = &vp;
            gp.pRasterizationState = &rs;
            gp.pMultisampleState = &ms;
            gp.pColorBlendState = &cb;
            gp.pDepthStencilState = &ds;
            gp.pDynamicState = &dsi;
            gp.layout = layout;
            gp.renderPass = shadowPass;
            gp.subpass = 0;
            ok = vkCreateGraphicsPipelines(dev.device, VK_NULL_HANDLE, 1, &gp,
                                           nullptr, &pipeline)
                 == VK_SUCCESS;
        }

        vkDestroyShaderModule(dev.device, vs, nullptr);
        vkDestroyShaderModule(dev.device, fs, nullptr);
        if (!ok) std::fprintf(stderr, "[vk] shadow pipeline creation failed\n");
        return ok;
    }

    void VulkanPipeline::destroy(const VulkanDevice& dev)
    {
        if (pipeline) {
            vkDestroyPipeline(dev.device, pipeline, nullptr);
            pipeline = VK_NULL_HANDLE;
        }
        if (layout) {
            vkDestroyPipelineLayout(dev.device, layout, nullptr);
            layout = VK_NULL_HANDLE;
        }
    }
} // namespace gpu
