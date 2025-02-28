/*
* (C) 2019-2020 see Authors.txt
*
* This file is part of MPC-BE.
*
* MPC-BE is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 3 of the License, or
* (at your option) any later version.
*
* MPC-BE is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*
*/

#pragma once

#include "stdafx.h"
#include <d3d11.h>
#include <dxgi1_6.h>
#include "Helper.h"
#include "DX11Helper.h"

D3D11_TEXTURE2D_DESC CreateTex2DDesc(const DXGI_FORMAT format, const UINT width, const UINT height, const Tex2DType type)
{
	D3D11_TEXTURE2D_DESC desc;
	desc.Width = width;
	desc.Height = height;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = format;
	desc.SampleDesc = { 1, 0 };

	switch (type) {
	default:
	case Tex2D_Default:
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = 0;
		desc.CPUAccessFlags = 0;
		desc.MiscFlags = 0;
		break;
	case Tex2D_DefaultRTarget:
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_RENDER_TARGET;
		desc.CPUAccessFlags = 0;
		desc.MiscFlags = 0;
		break;
	case Tex2D_DefaultShader:
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		desc.CPUAccessFlags = 0;
		desc.MiscFlags = 0;
		break;
	case Tex2D_DefaultShaderRTarget:
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
		desc.CPUAccessFlags = 0;
		desc.MiscFlags = 0;
		break;
	case Tex2D_DynamicShaderWrite:
	case Tex2D_DynamicShaderWriteNoSRV:
		desc.Usage = D3D11_USAGE_DYNAMIC;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		desc.MiscFlags = 0;
		break;
	case Tex2D_StagingRead:
		desc.Usage = D3D11_USAGE_STAGING;
		desc.BindFlags = 0;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		desc.MiscFlags = 0;
		break;
	}

	return desc;
}

UINT GetAdapter(HWND hWnd, IDXGIFactory1* pDXGIFactory, IDXGIAdapter** ppDXGIAdapter)
{
	*ppDXGIAdapter = nullptr;

	CheckPointer(pDXGIFactory, 0);

	const HMONITOR hMonitor = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);

	UINT i = 0;
	IDXGIAdapter* pDXGIAdapter = nullptr;
	while (SUCCEEDED(pDXGIFactory->EnumAdapters(i, &pDXGIAdapter))) {
		UINT k = 0;
		IDXGIOutput* pDXGIOutput = nullptr;
		while (SUCCEEDED(pDXGIAdapter->EnumOutputs(k, &pDXGIOutput))) {
			DXGI_OUTPUT_DESC desc = {};
			if (SUCCEEDED(pDXGIOutput->GetDesc(&desc))) {
				if (desc.Monitor == hMonitor) {
					SAFE_RELEASE(pDXGIOutput);
					*ppDXGIAdapter = pDXGIAdapter;
					return i;
				}
			}
			SAFE_RELEASE(pDXGIOutput);
			k++;
		}

		SAFE_RELEASE(pDXGIAdapter);
		i++;
	}

	return 0;
}

HRESULT DumpTexture2D(ID3D11DeviceContext* pDeviceContext, ID3D11Texture2D* pTexture2D, const wchar_t* filename)
{
	D3D11_TEXTURE2D_DESC desc;
	pTexture2D->GetDesc(&desc);

	if (desc.Format != DXGI_FORMAT_B8G8R8X8_UNORM
			&& desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM
			&& desc.Format != DXGI_FORMAT_AYUV
			&& desc.Format != DXGI_FORMAT_NV12) {
		return E_INVALIDARG;
	}

	HRESULT hr = S_OK;
	CComPtr<ID3D11Texture2D> pTexture2DShared;

	if (desc.CPUAccessFlags & D3D11_CPU_ACCESS_READ) {
		pTexture2DShared = pTexture2D;
	} else {
		ID3D11Device* pDevice;
		pTexture2D->GetDevice(&pDevice);
		D3D11_TEXTURE2D_DESC desc2 = CreateTex2DDesc(desc.Format, desc.Width, desc.Height, Tex2D_StagingRead);

		hr = pDevice->CreateTexture2D(&desc2, nullptr, &pTexture2DShared);
		pDevice->Release();

		//pDeviceContext->CopyResource(pTexture2DShared, pTexture2D);
		pDeviceContext->CopySubresourceRegion(pTexture2DShared, 0, 0, 0, 0, pTexture2D, 0, nullptr);
	}

	if (SUCCEEDED(hr)) {
		D3D11_MAPPED_SUBRESOURCE mappedResource = {};
		hr = pDeviceContext->Map(pTexture2DShared, 0, D3D11_MAP_READ, 0, &mappedResource);

		if (SUCCEEDED(hr)) {
			UINT width = desc.Width;
			UINT height = desc.Height;
			UINT bitdepth = 8;

			switch (desc.Format) {
			case DXGI_FORMAT_B8G8R8X8_UNORM:
			case DXGI_FORMAT_B8G8R8A8_UNORM:
			case DXGI_FORMAT_AYUV:
				bitdepth = 32;
				break;
			case DXGI_FORMAT_NV12:
				height = height * 3 / 2;
				break;
			}

			hr = SaveToBMP((BYTE*)mappedResource.pData, mappedResource.RowPitch, width, height, bitdepth, filename);

			pDeviceContext->Unmap(pTexture2DShared, 0);
		}
	}

	return hr;
}

DirectX::XMFLOAT4 TransferPQ(DirectX::XMFLOAT4& colorF, const float SDR_peak_lum)
{
	// https://github.com/thexai/xbmc/blob/master/system/shaders/guishader_common.hlsl
	const float ST2084_m1 = 2610.0f / (4096.0f * 4.0f);
	const float ST2084_m2 = (2523.0f / 4096.0f) * 128.0f;
	const float ST2084_c1 = 3424.0f / 4096.0f;
	const float ST2084_c2 = (2413.0f / 4096.0f) * 32.0f;
	const float ST2084_c3 = (2392.0f / 4096.0f) * 32.0f;
	const float matx[3][3] = {
		{0.627402f, 0.329292f, 0.043306f},
		{0.069095f, 0.919544f, 0.011360f},
		{0.016394f, 0.088028f, 0.895578f}
	};

	float c[3] = { colorF.x, colorF.y, colorF.z };

	for (unsigned i = 0; i < 3; i++) {
		// REC.709 to linear
		c[i] = pow(c[i], 1.0f / 0.45f);
		// REC.709 to BT.2020
		c[i] = matx[i][0] * c[0] + matx[i][1] * c[1] + matx[i][2] * c[2];
		// linear to PQ
		c[i] = pow(c[i] / SDR_peak_lum, ST2084_m1);
		c[i] = (ST2084_c1 + ST2084_c2 * c[i]) / (1.0f + ST2084_c3 * c[i]);
		c[i] = pow(c[i], ST2084_m2);
	}

	colorF.x = c[0];
	colorF.y = c[1];
	colorF.z = c[2];

	return colorF;
}

D3DCOLOR TransferPQ(const D3DCOLOR color, const float SDR_peak_lum)
{
	DirectX::XMFLOAT4 colorF = D3DCOLORtoXMFLOAT4(color);
	colorF = TransferPQ(colorF, SDR_peak_lum);
	return XMFLOAT4toD3DCOLOR(colorF);
}
